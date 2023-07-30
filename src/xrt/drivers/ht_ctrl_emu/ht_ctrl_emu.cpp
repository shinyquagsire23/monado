// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Driver to emulate controllers from hand-tracking input
 * @author Moses Turner <moses@collabora.com>
 * @author Nick Klingensmith <programmerpichu@gmail.com>
 *
 * @ingroup drv_cemu
 */

#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "os/os_time.h"

#include "math/m_api.h"
#include "math/m_space.h"
#include "math/m_vec3.h"

#include "util/u_var.h"
#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_distortion_mesh.h"
#include "util/u_config_json.h"

#include "ht_ctrl_emu_interface.h"

#include <assert.h>
#include <stdio.h>


static const float cm2m = 0.01f;

DEBUG_GET_ONCE_LOG_OPTION(cemu_log, "CEMU_LOG", U_LOGGING_TRACE)

#define CEMU_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->sys->log_level, __VA_ARGS__)
#define CEMU_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->sys->log_level, __VA_ARGS__)
#define CEMU_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->sys->log_level, __VA_ARGS__)
#define CEMU_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->sys->log_level, __VA_ARGS__)
#define CEMU_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->sys->log_level, __VA_ARGS__)

enum cemu_input_index
{
	CEMU_INDEX_HAND_TRACKING,
	CEMU_INDEX_SELECT,
	CEMU_INDEX_MENU,
	CEMU_INDEX_GRIP,
	CEMU_INDEX_AIM,
	CEMU_NUM_INPUTS,
};

static enum xrt_space_relation_flags valid_flags = (enum xrt_space_relation_flags)(
    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

struct cemu_system
{
	// We don't own the head - never free this
	struct xrt_device *in_head;
	// We "own" the hand, and it gets replaced by the out_hands. So once they are both freed we need to free the
	// original hand tracker
	struct xrt_device *in_hand;

	struct cemu_device *out_hand[2];

	float grip_offset_from_palm;

	float waggle, curl, twist;

	enum u_logging_level log_level;
};

struct cemu_device
{
	struct xrt_device base;
	struct cemu_system *sys;

	int hand_index;
	enum xrt_input_name ht_input_name;

	struct xrt_tracking_origin tracking_origin;
};

xrt_quat
wct_to_quat(float waggle, float curl, float twist)
{
	xrt_vec3 waggle_axis = {0, 1, 0};
	xrt_quat just_waggle;
	math_quat_from_angle_vector(waggle, &waggle_axis, &just_waggle);

	xrt_vec3 curl_axis = {1, 0, 0};
	xrt_quat just_curl;
	math_quat_from_angle_vector(curl, &curl_axis, &just_curl);

	xrt_vec3 twist_axis = {0, 0, 1};
	xrt_quat just_twist;
	math_quat_from_angle_vector(twist, &twist_axis, &just_twist);

	xrt_quat out = just_waggle; // Unnecessary but much easier to look at.

	math_quat_rotate(&out, &just_curl, &out);
	math_quat_rotate(&out, &just_twist, &out);
	return out;
}

static inline struct cemu_device *
cemu_device(struct xrt_device *xdev)
{
	return (struct cemu_device *)xdev;
}


static void
cemu_device_destroy(struct xrt_device *xdev)
{
	struct cemu_device *dev = cemu_device(xdev);
	struct cemu_system *system = dev->sys;

	// Remove the variable tracking.
	u_device_free(&system->out_hand[dev->hand_index]->base);

	system->out_hand[dev->hand_index] = NULL;

	if ((system->out_hand[0] == NULL) && (system->out_hand[1] == NULL)) {
		xrt_device_destroy(&system->in_hand);
		u_var_remove_root(system);
		free(system);
	}
}

static void
cemu_device_get_hand_tracking(struct xrt_device *xdev,
                              enum xrt_input_name name,
                              uint64_t requested_timestamp_ns,
                              struct xrt_hand_joint_set *out_value,
                              uint64_t *out_timestamp_ns)
{
	// Shadows normal hand tracking - does nothing differently

	struct cemu_device *dev = cemu_device(xdev);
	struct cemu_system *system = dev->sys;

	if (name != dev->ht_input_name) {
		// I should be using xrt_input_name_string here - couldn't figure out how to link to it.
		CEMU_ERROR(dev, "unexpected input name %d - expected %d", name, dev->ht_input_name);
		return;
	}

	xrt_device_get_hand_tracking(system->in_hand, dev->ht_input_name, requested_timestamp_ns, out_value,
	                             out_timestamp_ns);
}

static xrt_vec3
joint_position_global(xrt_hand_joint_set *joint_set, xrt_hand_joint joint)
{
	struct xrt_space_relation out_relation;
	struct xrt_relation_chain xrc = {};
	m_relation_chain_push_relation(&xrc, &joint_set->values.hand_joint_set_default[joint].relation);
	m_relation_chain_push_relation(&xrc, &joint_set->hand_pose);
	m_relation_chain_resolve(&xrc, &out_relation);
	return out_relation.pose.position;
}

static xrt_pose
joint_pose_global(xrt_hand_joint_set *joint_set, xrt_hand_joint joint)
{
	struct xrt_space_relation out_relation;
	struct xrt_relation_chain xrc = {};
	m_relation_chain_push_relation(&xrc, &joint_set->values.hand_joint_set_default[joint].relation);
	m_relation_chain_push_relation(&xrc, &joint_set->hand_pose);
	m_relation_chain_resolve(&xrc, &out_relation);
	return out_relation.pose;
}

static void
do_grip_pose(struct xrt_hand_joint_set *joint_set,
             struct xrt_space_relation *out_relation,
             float grip_offset_from_palm,
             bool is_right)
{

	xrt_pose offset_from_palm;
	math_pose_identity(&offset_from_palm);
	offset_from_palm.position.y = -grip_offset_from_palm;
	xrt_pose palm = joint_pose_global(joint_set, XRT_HAND_JOINT_PALM);

	// Position.
	struct xrt_relation_chain xrc = {};
	m_relation_chain_push_pose(&xrc, &offset_from_palm);
	m_relation_chain_push_pose(&xrc, &palm);
	m_relation_chain_resolve(&xrc, out_relation);


	// Orientation.
	xrt_vec3 indx_position = joint_position_global(joint_set, XRT_HAND_JOINT_INDEX_PROXIMAL);
	xrt_vec3 ring_position = joint_position_global(joint_set, XRT_HAND_JOINT_RING_PROXIMAL);
	struct xrt_vec3 plus_z = ring_position - indx_position;
	struct xrt_vec3 plus_x;
	struct xrt_vec3 to_rotate = {0.0f, is_right ? 1.0f : -1.0f, 0.0f};

	math_quat_rotate_vec3(&palm.orientation, &to_rotate, &plus_x);

	plus_x = m_vec3_orthonormalize(plus_z, plus_x);

	math_vec3_normalize(&plus_x);
	math_vec3_normalize(&plus_z);

	math_quat_from_plus_x_z(&plus_x, &plus_z, &out_relation->pose.orientation);

	out_relation->relation_flags = valid_flags;
}



static void
get_other_two(struct cemu_device *dev,
              uint64_t head_timestamp_ns,
              uint64_t hand_timestamp_ns,
              xrt_pose *out_head,
              xrt_hand_joint_set *out_secondary)
{
	struct xrt_space_relation head_rel;
	xrt_device_get_tracked_pose(dev->sys->in_head, XRT_INPUT_GENERIC_HEAD_POSE, head_timestamp_ns, &head_rel);
	*out_head = head_rel.pose;
	int other;
	if (dev->hand_index == 0) {
		other = 1;
	} else {
		other = 0;
	}
	uint64_t noop;

	xrt_device_get_hand_tracking(dev->sys->in_hand, dev->sys->out_hand[other]->ht_input_name, hand_timestamp_ns,
	                             out_secondary, &noop);
}

// Mostly stolen from
// https://github.com/maluoi/StereoKit/blob/048b689f71d080a67fde29838c0362a49b88b3d6/StereoKitC/systems/hand/hand_oxr_articulated.cpp#L149
static void
do_aim_pose(struct cemu_device *dev,
            struct xrt_hand_joint_set *joint_set_primary,
            uint64_t head_timestamp_ns,
            uint64_t hand_timestamp_ns,
            struct xrt_space_relation *out_relation)
{
	struct xrt_vec3 vec3_up = {0, 1, 0};
	struct xrt_pose head;
	struct xrt_hand_joint_set joint_set_secondary;
#if 0
	// "Jakob way"
	get_other_two(dev, hand_timestamp_ns, hand_timestamp_ns, &head, &joint_set_secondary);
#else
	// "Moses way"
	get_other_two(dev, head_timestamp_ns, hand_timestamp_ns, &head, &joint_set_secondary);
#endif


	// Average shoulder width for women:37cm, men:41cm, center of shoulder
	// joint is around 4cm inwards
	const float avg_shoulder_width = ((39.0f / 2.0f) - 4.0f) * cm2m;
	const float head_length = 10 * cm2m;
	const float neck_length = 7 * cm2m;

	// Chest center is down to the base of the head, and then down the neck.
	xrt_vec3 down_the_base_of_head;
	xrt_vec3 base_head_direction = {0, -head_length, 0};

	math_quat_rotate_vec3(&head.orientation, &base_head_direction, &down_the_base_of_head);

	xrt_vec3 chest_center = head.position + down_the_base_of_head + xrt_vec3{0, -neck_length, 0};

	xrt_vec3 face_fwd;
	xrt_vec3 forwards = {0, 0, -1};

	math_quat_rotate_vec3(&head.orientation, &forwards, &face_fwd);

	face_fwd = m_vec3_mul_scalar(m_vec3_normalize(face_fwd), 2);
	face_fwd += m_vec3_mul_scalar(
	    m_vec3_normalize(joint_position_global(joint_set_primary, XRT_HAND_JOINT_WRIST) - chest_center), 1);
	if (joint_set_secondary.is_active) {
		face_fwd += m_vec3_mul_scalar(
		    m_vec3_normalize(joint_position_global(&joint_set_secondary, XRT_HAND_JOINT_WRIST) - chest_center),
		    1);
	}
	face_fwd.y = 0;
	m_vec3_normalize(face_fwd);

	xrt_vec3 face_right;
	math_vec3_cross(&face_fwd, &vec3_up, &face_right);
	math_vec3_normalize(&face_right);
	face_right *= avg_shoulder_width;

	xrt_vec3 shoulder = chest_center + face_right * (dev->hand_index == 1 ? 1.0f : -1.0f);

	xrt_vec3 ray_joint = joint_position_global(joint_set_primary, XRT_HAND_JOINT_INDEX_PROXIMAL);

	struct xrt_vec3 ray_direction = shoulder - ray_joint;

	struct xrt_vec3 up = {0, 1, 0};

	struct xrt_vec3 out_x_vector;

	// math_vec3_normalize(&tip_to_palm);
	math_vec3_normalize(&ray_direction);

	math_vec3_cross(&up, &ray_direction, &out_x_vector);

	out_relation->pose.position = ray_joint;

	math_quat_from_plus_x_z(&out_x_vector, &ray_direction, &out_relation->pose.orientation);

	out_relation->relation_flags = valid_flags;
}

// Pose for controller emulation
static void
cemu_device_get_tracked_pose(struct xrt_device *xdev,
                             enum xrt_input_name name,
                             uint64_t at_timestamp_ns,
                             struct xrt_space_relation *out_relation)
{
	struct cemu_device *dev = cemu_device(xdev);
	struct cemu_system *sys = dev->sys;

	if (name != XRT_INPUT_SIMPLE_GRIP_POSE && name != XRT_INPUT_SIMPLE_AIM_POSE) {
		CEMU_ERROR(dev, "unknown input name %d for controller pose", name);
		return;
	}
	static uint64_t hand_timestamp_ns;

	struct xrt_hand_joint_set joint_set;
	sys->in_hand->get_hand_tracking(sys->in_hand, dev->ht_input_name, at_timestamp_ns, &joint_set,
	                                &hand_timestamp_ns);

	if (joint_set.is_active == false) {
		out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;
		return;
	}



	switch (name) {
	case XRT_INPUT_SIMPLE_GRIP_POSE: {
		do_grip_pose(&joint_set, out_relation, sys->grip_offset_from_palm, dev->hand_index);
		break;
	}
	case XRT_INPUT_SIMPLE_AIM_POSE: {
		// Assume that now we're doing everything in the timestamp from the hand-tracker, so use
		// hand_timestamp_ns. This will cause the controller to lag behind but otherwise be correct
		do_aim_pose(dev, &joint_set, at_timestamp_ns, hand_timestamp_ns, out_relation);
		break;
	}
	default: assert(false);
	}
}

static void
cemu_device_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	// No-op, needed to avoid crash.
}

//! @todo This is flickery; investigate once we get better hand tracking
static void
decide(xrt_vec3 one, xrt_vec3 two, bool *out)
{
	float dist = m_vec3_len_sqrd(one - two);
	// These used to be 0.02f and 0.04f, but I bumped them way up to compensate for bad tracking. Once our tracking
	// is better, bump these back down.
	float activation_dist = 0.02f;
	float deactivation_dist = 0.04f;
	const float pinch_activation_dist =
	    (*out ? deactivation_dist * deactivation_dist : activation_dist * activation_dist);

	*out = (dist < pinch_activation_dist);
}

static void
cemu_device_update_inputs(struct xrt_device *xdev)
{
	struct cemu_device *dev = cemu_device(xdev);

	struct xrt_hand_joint_set joint_set;
	uint64_t noop;

	xrt_device_get_hand_tracking(dev->sys->in_hand, dev->ht_input_name, os_monotonic_get_ns(), &joint_set, &noop);


	if (!joint_set.is_active) {
		xdev->inputs[CEMU_INDEX_SELECT].value.boolean = false;
		xdev->inputs[CEMU_INDEX_MENU].value.boolean = false;
		return;
	}

	decide(joint_set.values.hand_joint_set_default[XRT_HAND_JOINT_INDEX_TIP].relation.pose.position,
	       joint_set.values.hand_joint_set_default[XRT_HAND_JOINT_THUMB_TIP].relation.pose.position,
	       &xdev->inputs[CEMU_INDEX_SELECT].value.boolean);

	// For now, all other inputs are off - detecting any gestures more complicated than pinch is too unreliable for
	// now.
	xdev->inputs[CEMU_INDEX_MENU].value.boolean = false;
}


extern "C" int
cemu_devices_create(struct xrt_device *head, struct xrt_device *hands, struct xrt_device **out_xdevs)
{
	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_NO_FLAGS;

	struct cemu_device *cemud[2];

	struct cemu_system *system = U_TYPED_CALLOC(struct cemu_system);
	system->in_hand = hands;
	system->in_head = head;

	system->log_level = debug_get_log_option_cemu_log();

	system->grip_offset_from_palm = 0.03f; // 3 centimeters

	for (int i = 0; i < 2; i++) {
		cemud[i] = U_DEVICE_ALLOCATE(struct cemu_device, flags, CEMU_NUM_INPUTS, 0);

		cemud[i]->sys = system;

		cemud[i]->base.tracking_origin = hands->tracking_origin;

		cemud[i]->base.name = XRT_DEVICE_SIMPLE_CONTROLLER;
		cemud[i]->base.hand_tracking_supported = true;
		cemud[i]->base.orientation_tracking_supported = true;
		cemud[i]->base.position_tracking_supported = true;


		cemud[i]->base.inputs[CEMU_INDEX_HAND_TRACKING].name =
		    i ? XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT : XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
		cemud[i]->base.inputs[CEMU_INDEX_SELECT].name = XRT_INPUT_SIMPLE_SELECT_CLICK;
		cemud[i]->base.inputs[CEMU_INDEX_MENU].name = XRT_INPUT_SIMPLE_MENU_CLICK;
		cemud[i]->base.inputs[CEMU_INDEX_GRIP].name = XRT_INPUT_SIMPLE_GRIP_POSE;
		cemud[i]->base.inputs[CEMU_INDEX_AIM].name = XRT_INPUT_SIMPLE_AIM_POSE;

		cemud[i]->base.update_inputs = cemu_device_update_inputs;
		cemud[i]->base.get_tracked_pose = cemu_device_get_tracked_pose;
		cemud[i]->base.set_output = cemu_device_set_output;
		cemud[i]->base.get_hand_tracking = cemu_device_get_hand_tracking;
		cemud[i]->base.destroy = cemu_device_destroy;

		cemud[i]->base.device_type =
		    i ? XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER : XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;

		int n =
		    snprintf(cemud[i]->base.str, XRT_DEVICE_NAME_LEN, "%s %s Hand", i ? "Right" : "Left", hands->str);
		if (n > XRT_DEVICE_NAME_LEN) {
			CEMU_DEBUG(cemud[i], "name truncated: %s", cemud[i]->base.str);
		}

		n = snprintf(cemud[i]->base.serial, XRT_DEVICE_NAME_LEN, "%s (%d)", hands->str, i);
		if (n > XRT_DEVICE_NAME_LEN) {
			CEMU_WARN(cemud[i], "serial truncated: %s", cemud[i]->base.str);
		}

		cemud[i]->ht_input_name =
		    i ? XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT : XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;

		cemud[i]->hand_index = i;
		system->out_hand[i] = cemud[i];

		out_xdevs[i] = &cemud[i]->base;
	}

	u_var_add_root(system, "Controller emulation!", true);
	u_var_add_f32(system, &system->grip_offset_from_palm, "Grip pose offset");

	return 2;

	// We actually don't need these - no failure condition yet. Uncomment whenever you need 'em
	// cleanup:
	// 	cemu_device_destroy(&cemud[0]->base);
	// 	cemu_device_destroy(&cemud[1]->base);
	// 	return 0;
}
