// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: Apache-2.0
/*!
 * @file
 * @brief  Adapter to Libsurvive.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_survive
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <string.h>
#include <inttypes.h>

#include "math/m_api.h"
#include "xrt/xrt_device.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_device.h"
#include "util/u_distortion_mesh.h"

#include "../auxiliary/os/os_time.h"

#include "xrt/xrt_prober.h"
#include "survive_interface.h"

#include "survive_api.h"

#include "survive_wrap.h"

#include "util/u_json.h"

#include "util/u_hand_tracking.h"
#include "util/u_logging.h"

#include "math/m_predict.h"

// typically HMD config is available at around 2 seconds after init
#define WAIT_TIMEOUT 5.0

// public documentation
//! @todo move to vive_protocol
#define INDEX_MIN_IPD 0.058
#define INDEX_MAX_IPD 0.07

#define DEFAULT_HAPTIC_FREQ 150.0f
#define MIN_HAPTIC_DURATION 0.05f

#define SURVIVE_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->sys->ll, __VA_ARGS__)
#define SURVIVE_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->sys->ll, __VA_ARGS__)
#define SURVIVE_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->sys->ll, __VA_ARGS__)
#define SURVIVE_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->sys->ll, __VA_ARGS__)
#define SURVIVE_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->sys->ll, __VA_ARGS__)

struct survive_system;

enum input_index
{
	// common inputs
	VIVE_CONTROLLER_AIM_POSE = 0,
	VIVE_CONTROLLER_GRIP_POSE,
	VIVE_CONTROLLER_SYSTEM_CLICK,
	VIVE_CONTROLLER_TRIGGER_CLICK,
	VIVE_CONTROLLER_TRIGGER_VALUE,
	VIVE_CONTROLLER_TRACKPAD,
	VIVE_CONTROLLER_TRACKPAD_TOUCH,

	// Vive Wand specific inputs
	VIVE_CONTROLLER_SQUEEZE_CLICK,
	VIVE_CONTROLLER_MENU_CLICK,
	VIVE_CONTROLLER_TRACKPAD_CLICK,

	// Valve Index specific inputs
	VIVE_CONTROLLER_THUMBSTICK,
	VIVE_CONTROLLER_A_CLICK,
	VIVE_CONTROLLER_B_CLICK,
	VIVE_CONTROLLER_THUMBSTICK_CLICK,
	VIVE_CONTROLLER_THUMBSTICK_TOUCH,
	VIVE_CONTROLLER_SYSTEM_TOUCH,
	VIVE_CONTROLLER_A_TOUCH,
	VIVE_CONTROLLER_B_TOUCH,
	VIVE_CONTROLLER_SQUEEZE_VALUE,
	VIVE_CONTROLLER_SQUEEZE_FORCE,
	VIVE_CONTROLLER_TRIGGER_TOUCH,
	VIVE_CONTROLLER_TRACKPAD_FORCE,

	VIVE_CONTROLLER_HAND_TRACKING,

	VIVE_CONTROLLER_MAX_INDEX,
};

// also used as index in sys->controllers[] array
typedef enum
{
	SURVIVE_LEFT_CONTROLLER = 0,
	SURVIVE_RIGHT_CONTROLLER = 1,
	SURVIVE_HMD = 2,
} SurviveDeviceType;

static bool survive_already_initialized = false;

enum VIVE_VARIANT
{
	VIVE_UNKNOWN = 0,
	VIVE_VARIANT_VIVE,
	VIVE_VARIANT_PRO,
	VIVE_VARIANT_VALVE_INDEX,
	VIVE_VARIANT_HTC_VIVE_CONTROLLER,
	VIVE_VARIANT_VALVE_INDEX_LEFT_CONTROLLER,
	VIVE_VARIANT_VALVE_INDEX_RIGHT_CONTROLLER,
	VIVE_VARIANT_TRACKER_V1,
	VIVE_VARIANT_TRACKER_v2
};

/*!
 * @implements xrt_device
 */
struct survive_device
{
	struct xrt_device base;
	struct survive_system *sys;
	const SurviveSimpleObject *survive_obj;

	struct xrt_space_relation last_relation;

	int num;

	enum VIVE_VARIANT variant;

	union {
		struct
		{
			float proximity; // [0,1]
			float ipd;
			struct xrt_quat rot[2];
		} hmd;

		struct
		{
			float curl[XRT_FINGER_COUNT];
			uint64_t curl_ts[XRT_FINGER_COUNT];
			struct u_hand_tracking hand_tracking;
		} ctrl;
	};
	struct u_vive_values distortion[2];
};

//! @todo support more devices (trackers, ...)
#define CONTROLLER_COUNT 2

/*!
 * @extends xrt_tracking_origin
 */
struct survive_system
{
	struct xrt_tracking_origin base;
	SurviveSimpleContext *ctx;
	struct survive_device *hmd;
	struct survive_device *controllers[CONTROLLER_COUNT];
	enum u_logging_level ll;
};

static void
survive_device_destroy(struct xrt_device *xdev)
{
	U_LOG_D("destroying survive device");
	struct survive_device *survive = (struct survive_device *)xdev;

	if (survive == survive->sys->hmd)
		survive->sys->hmd = NULL;
	if (survive == survive->sys->controllers[SURVIVE_LEFT_CONTROLLER])
		survive->sys->controllers[SURVIVE_LEFT_CONTROLLER] = NULL;
	if (survive == survive->sys->controllers[SURVIVE_RIGHT_CONTROLLER])
		survive->sys->controllers[SURVIVE_RIGHT_CONTROLLER] = NULL;

	if (survive->sys->hmd == NULL && survive->sys->controllers[SURVIVE_LEFT_CONTROLLER] == NULL &&
	    survive->sys->controllers[SURVIVE_RIGHT_CONTROLLER] == NULL) {
		U_LOG_D("Tearing down libsurvive context");
		survive_simple_close(survive->sys->ctx);

		free(survive->sys);
	}

	free(survive);
}

// libsurvive timecode may not be exactly comparable with monotonic ns.
// see OGGetAbsoluteTimeUS in libsurvive redist/os_generic.unix.h
static double
survive_timecode_now_s()
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return ((double)tv.tv_usec) / 1000000. + (tv.tv_sec);
}

static void
_get_survive_pose(struct survive_device *survive, uint64_t at_timestamp_ns, struct xrt_space_relation *out_relation)
{
	const SurviveSimpleObject *survive_object = survive->survive_obj;

	out_relation->relation_flags = XRT_SPACE_RELATION_BITMASK_NONE;

	if (survive_simple_object_get_type(survive_object) != SurviveSimpleObject_OBJECT &&
	    survive_simple_object_get_type(survive_object) != SurviveSimpleObject_HMD) {
		return;
	}

	// Initially pose can be zeroed. That's okay, we report it without
	// orientation etc. valid flag then.
	SurvivePose pose;
	SurviveVelocity vel;

	// "device time" in seconds
	double timecode_s = survive_simple_object_get_latest_pose(survive_object, &pose);

	double vel_timecode_s = survive_simple_object_get_latest_velocity(survive_object, &vel);
	(void)vel_timecode_s;

	// do calculations in ns due to large numbers
	timepoint_ns timecode_ns = time_s_to_ns(timecode_s);

	timepoint_ns monotonic_now_ns = os_monotonic_get_ns();
	timepoint_ns remaining_ns = at_timestamp_ns - monotonic_now_ns;

	timepoint_ns survive_now_ns = time_s_to_ns(survive_timecode_now_s());
	timepoint_ns survive_pose_age_ns = survive_now_ns - timecode_ns;

	timepoint_ns prediction_ns = remaining_ns + survive_pose_age_ns;

	double prediction_s = time_ns_to_s(prediction_ns);

	SURVIVE_TRACE(survive,
	              "dev %s At %ldns: Pose requested for +%ldns (%ldns). "
	              "Libsurvive Pose Timecode: %ldns, Pose gotten at -%ldns "
	              "from now (%ldns), predicting %ldns",
	              survive->base.str, monotonic_now_ns, remaining_ns, at_timestamp_ns, timecode_ns,
	              survive_pose_age_ns, survive_now_ns, prediction_ns);

	struct xrt_quat out_rot = {.x = pose.Rot[1], .y = pose.Rot[2], .z = pose.Rot[3], .w = pose.Rot[0]};

	/* libsurvive looks down when it should be looking forward, so
	 * rotate the quat.
	 * because the HMD quat is the opposite of the in world
	 * rotation, we rotate down. */

	struct xrt_quat down_rot;
	down_rot.x = sqrtf(2) / 2.;
	down_rot.y = 0;
	down_rot.z = 0;
	down_rot.w = -sqrtf(2) / 2.;

	math_quat_rotate(&down_rot, &out_rot, &out_rot);

	// just to be sure
	math_quat_normalize(&out_rot);



	out_relation->pose.orientation = out_rot;

	/* switch -y, z axes to go from libsurvive coordinate system to ours */
	out_relation->pose.position.x = pose.Pos[0];
	out_relation->pose.position.y = pose.Pos[2];
	out_relation->pose.position.z = -pose.Pos[1];

	struct xrt_vec3 linear_vel = {.x = vel.Pos[0], .y = vel.Pos[2], .z = -vel.Pos[1]};

	struct xrt_vec3 angular_vel = {.x = vel.AxisAngleRot[0], .y = vel.AxisAngleRot[2], .z = -vel.AxisAngleRot[1]};

	if (math_quat_validate(&out_rot)) {
		out_relation->relation_flags |=
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;

		// everything else is invalid if orientation is not valid

		if (math_vec3_validate(&out_relation->pose.position)) {
			out_relation->relation_flags |=
			    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
		}


		out_relation->linear_velocity = linear_vel;
		if (math_vec3_validate(&out_relation->linear_velocity)) {
			out_relation->relation_flags |= XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT;
		}

		out_relation->angular_velocity = angular_vel;
		if (math_vec3_validate(&out_relation->angular_velocity)) {
			out_relation->relation_flags |= XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT;
		}
	}

	SURVIVE_TRACE(survive, "Predicting %fs for %s", prediction_s, survive->base.str);

	struct xrt_space_relation rel = *out_relation;
	m_predict_relation(&rel, prediction_s, out_relation);
}

//! @todo: Hotplugging, with get_variant_from_json()
#if 0
static bool
_update_survive_devices(struct survive_system *sys)
{
	//! @todo better method

	if (sys->hmd->survive_obj && sys->controllers[0]->survive_obj &&
	    sys->controllers[1]->survive_obj)
		return true;

	SurviveSimpleContext *ctx = sys->ctx;

	for (const SurviveSimpleObject *it =
	         survive_simple_get_first_object(ctx);
	     it != 0; it = survive_simple_get_next_object(ctx, it)) {
		const char *codename = survive_simple_object_name(it);

		enum SurviveSimpleObject_type type =
		    survive_simple_object_get_type(it);
		if (type == SurviveSimpleObject_HMD &&
		    sys->hmd->survive_obj == NULL) {
			U_LOG_D("Found HMD: %s", codename);
			sys->hmd->survive_obj = it;
		}
		if (type == SurviveSimpleObject_OBJECT) {
			for (int i = 0; i < CONTROLLER_COUNT; i++) {
				if (sys->controllers[i]->survive_obj == it) {
					break;
				}

				if (sys->controllers[i]->survive_obj == NULL) {
					U_LOG_D("Found Controller %d: %s", i,
					        codename);
					sys->controllers[i]->survive_obj = it;
					break;
				}
			}
		}
	}

	return true;
}
#endif

static void
survive_device_get_tracked_pose(struct xrt_device *xdev,
                                enum xrt_input_name name,
                                uint64_t at_timestamp_ns,
                                struct xrt_space_relation *out_relation)
{
	struct survive_device *survive = (struct survive_device *)xdev;
	if ((survive == survive->sys->hmd && name != XRT_INPUT_GENERIC_HEAD_POSE) ||
	    ((survive == survive->sys->controllers[0] || survive == survive->sys->controllers[1]) &&
	     (name != XRT_INPUT_INDEX_AIM_POSE && name != XRT_INPUT_INDEX_GRIP_POSE) &&
	     (name != XRT_INPUT_VIVE_AIM_POSE && name != XRT_INPUT_VIVE_GRIP_POSE))) {

		SURVIVE_ERROR(survive, "unknown input name");
		return;
	}

	//_update_survive_devices(survive->sys);
	if (!survive->survive_obj) {
		// U_LOG_D("Obj not set for %p", (void*)survive);
		return;
	}


	_get_survive_pose(survive, at_timestamp_ns, out_relation);

	survive->last_relation = *out_relation;

	struct xrt_pose *p = &out_relation->pose;
	SURVIVE_TRACE(survive, "GET_POSITION (%f %f %f) GET_ORIENTATION (%f, %f, %f, %f)", p->position.x, p->position.y,
	              p->position.z, p->orientation.x, p->orientation.y, p->orientation.z, p->orientation.w);
}

static int
survive_controller_haptic_pulse(struct survive_device *survive, union xrt_output_value *value)
{
	float duration_seconds;
	if (value->vibration.duration == XRT_MIN_HAPTIC_DURATION) {
		SURVIVE_TRACE(survive, "Haptic pulse duration: using %f minimum", MIN_HAPTIC_DURATION);
		duration_seconds = MIN_HAPTIC_DURATION;
	} else {
		duration_seconds = time_ns_to_s(value->vibration.duration);
	}

	float frequency = value->vibration.frequency;

	if (frequency == XRT_FREQUENCY_UNSPECIFIED) {
		SURVIVE_TRACE(survive, "Haptic pulse frequency unspecified, setting to %fHz", DEFAULT_HAPTIC_FREQ);
		frequency = DEFAULT_HAPTIC_FREQ;
	}

	float amplitude = value->vibration.amplitude;

	SURVIVE_TRACE(survive, "Got Haptic pulse amp %f, %fHz, %" PRId64 "ns", value->vibration.amplitude,
	              value->vibration.frequency, value->vibration.duration);
	SURVIVE_TRACE(survive, "Doing Haptic pulse amp %f, %fHz, %fs", amplitude, frequency, duration_seconds);

	return survive_simple_object_haptic((struct SurviveSimpleObject *)survive->survive_obj, frequency, amplitude,
	                                    duration_seconds);
}

static void
survive_controller_device_set_output(struct xrt_device *xdev, enum xrt_output_name name, union xrt_output_value *value)
{
	struct survive_device *survive = (struct survive_device *)xdev;

	if (name != XRT_OUTPUT_NAME_VIVE_HAPTIC && name != XRT_OUTPUT_NAME_INDEX_HAPTIC) {
		SURVIVE_ERROR(survive, "Unknown output");
		return;
	}

	bool pulse = value->vibration.amplitude > 0.01;
	if (!pulse) {
		return;
	}

	int ret = survive_controller_haptic_pulse(survive, value);

	if (ret != 0) {
		SURVIVE_ERROR(survive, "haptic failed %d", ret);
	}
}

#define PI 3.14159265358979323846
#define DEG_TO_RAD(DEG) (DEG * PI / 180.)

static void
survive_controller_get_hand_tracking(struct xrt_device *xdev,
                                     enum xrt_input_name name,
                                     uint64_t at_timestamp_ns,
                                     struct xrt_hand_joint_set *out_value)
{
	struct survive_device *survive = (struct survive_device *)xdev;

	if (name != XRT_INPUT_GENERIC_HAND_TRACKING_LEFT && name != XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
		SURVIVE_ERROR(survive, "unknown input name for hand tracker");
		return;
	}


	bool left = survive->variant == VIVE_VARIANT_VALVE_INDEX_LEFT_CONTROLLER;
	enum xrt_hand hand = left ? XRT_HAND_LEFT : XRT_HAND_RIGHT;

	float thumb_curl = 0.0f;
	//! @todo place thumb preciely on the button that is touched/pressed
	if (survive->base.inputs[VIVE_CONTROLLER_A_TOUCH].value.boolean ||
	    survive->base.inputs[VIVE_CONTROLLER_B_TOUCH].value.boolean ||
	    survive->base.inputs[VIVE_CONTROLLER_THUMBSTICK_TOUCH].value.boolean ||
	    survive->base.inputs[VIVE_CONTROLLER_TRACKPAD_TOUCH].value.boolean) {
		thumb_curl = 1.0;
	}

	struct u_hand_tracking_curl_values values = {.little = survive->ctrl.curl[XRT_FINGER_LITTLE],
	                                             .ring = survive->ctrl.curl[XRT_FINGER_RING],
	                                             .middle = survive->ctrl.curl[XRT_FINGER_MIDDLE],
	                                             .index = survive->ctrl.curl[XRT_FINGER_INDEX],
	                                             .thumb = thumb_curl};

	/* The tracked controller position is at the very -z end of the
	 * controller. Move the hand back offset_z meter to the handle center.
	 */
	struct xrt_vec3 static_offset = {.x = 0, .y = 0, .z = 0.11};

	u_hand_joints_update_curl(&survive->ctrl.hand_tracking, hand, at_timestamp_ns, &values);

	struct xrt_pose hand_on_handle_pose;
	u_hand_joints_offset_valve_index_controller(hand, &static_offset, &hand_on_handle_pose);

	u_hand_joints_set_out_data(&survive->ctrl.hand_tracking, hand, &survive->last_relation, &hand_on_handle_pose,
	                           out_value);
}

static void
survive_device_get_view_pose(struct xrt_device *xdev,
                             struct xrt_vec3 *eye_relation,
                             uint32_t view_index,
                             struct xrt_pose *out_pose)
{
	struct xrt_pose pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
	bool adjust = view_index == 0;

	struct survive_device *survive = (struct survive_device *)xdev;
	pose.orientation = survive->hmd.rot[view_index];
	pose.position.x = eye_relation->x / 2.0f;
	pose.position.y = eye_relation->y / 2.0f;
	pose.position.z = eye_relation->z / 2.0f;

	// Adjust for left/right while also making sure there aren't any -0.f.
	if (pose.position.x > 0.0f && adjust) {
		pose.position.x = -pose.position.x;
	}
	if (pose.position.y > 0.0f && adjust) {
		pose.position.y = -pose.position.y;
	}
	if (pose.position.z > 0.0f && adjust) {
		pose.position.z = -pose.position.z;
	}

	*out_pose = pose;
}

enum InputComponent
{
	COMP_1D,
	COMP_2DX,
	COMP_2DY
};

struct Axis
{
	enum input_index input;
	enum InputComponent comp;
};

struct Axis axes[255] = {
    [SURVIVE_AXIS_TRIGGER] =
        {
            .input = VIVE_CONTROLLER_TRIGGER_VALUE,
            .comp = COMP_1D,
        },
    [SURVIVE_AXIS_TRACKPAD_X] =
        {
            .input = VIVE_CONTROLLER_TRACKPAD,
            .comp = COMP_2DX,
        },
    [SURVIVE_AXIS_TRACKPAD_Y] =
        {
            .input = VIVE_CONTROLLER_TRACKPAD,
            .comp = COMP_2DY,
        },
    [SURVIVE_AXIS_JOYSTICK_X] =
        {
            .input = VIVE_CONTROLLER_THUMBSTICK,
            .comp = COMP_2DX,
        },
    [SURVIVE_AXIS_JOYSTICK_Y] =
        {
            .input = VIVE_CONTROLLER_THUMBSTICK,
            .comp = COMP_2DY,
        },
    [SURVIVE_AXIS_GRIP_FORCE] =
        {
            .input = VIVE_CONTROLLER_SQUEEZE_FORCE,
            .comp = COMP_1D,
        },
    [SURVIVE_AXIS_TRACKPAD_FORCE] =
        {
            .input = VIVE_CONTROLLER_TRACKPAD_FORCE,
            .comp = COMP_1D,
        },
};

static bool
update_axis(struct survive_device *survive, struct Axis *axis, const SurviveSimpleButtonEvent *e, int i, uint64_t now)
{
	if (axis->input == 0) {
		return false;
	}

	struct xrt_input *in = &survive->base.inputs[axis->input];

	float fval = e->axis_val[i];

	switch (axis->comp) {
	case COMP_1D: in->value.vec1.x = fval; break;
	case COMP_2DX: in->value.vec2.x = fval; break;
	case COMP_2DY: in->value.vec2.y = fval; break;
	default: SURVIVE_WARN(survive, "Unknown axis component %d", axis->comp);
	}

	// SURVIVE_DEBUG("input %u Axis %d: %f", axis->input, i, fval);

	in->timestamp = now;
	return true;
}

struct Button
{
	enum input_index click;
	enum input_index touch;
};

struct Button buttons[255] = {
    [SURVIVE_BUTTON_A] = {.click = VIVE_CONTROLLER_A_CLICK, .touch = VIVE_CONTROLLER_A_TOUCH},
    [SURVIVE_BUTTON_B] = {.click = VIVE_CONTROLLER_B_CLICK, .touch = VIVE_CONTROLLER_B_TOUCH},

    [SURVIVE_BUTTON_TRACKPAD] = {.click = VIVE_CONTROLLER_TRACKPAD_CLICK, .touch = VIVE_CONTROLLER_TRACKPAD_TOUCH},

    [SURVIVE_BUTTON_THUMBSTICK] = {.click = VIVE_CONTROLLER_THUMBSTICK_CLICK,
                                   .touch = VIVE_CONTROLLER_THUMBSTICK_TOUCH},

    [SURVIVE_BUTTON_SYSTEM] = {.click = VIVE_CONTROLLER_SYSTEM_CLICK, .touch = VIVE_CONTROLLER_SYSTEM_TOUCH},

    [SURVIVE_BUTTON_MENU] = {.click = VIVE_CONTROLLER_MENU_CLICK,
                             // only on vive wand without touch
                             .touch = 0},

    [SURVIVE_BUTTON_GRIP] = {.click = VIVE_CONTROLLER_SQUEEZE_CLICK,
                             // only on vive wand without touch
                             .touch = 0},

    [SURVIVE_BUTTON_TRIGGER] = {.click = VIVE_CONTROLLER_TRIGGER_CLICK, .touch = VIVE_CONTROLLER_TRIGGER_TOUCH},
};

static bool
update_button(struct survive_device *survive, const struct SurviveSimpleButtonEvent *e, uint64_t now)
{
	if (e->event_type == SURVIVE_INPUT_EVENT_NONE) {
		return true;
	}

	enum SurviveButton btn_id = e->button_id;
	enum SurviveInputEvent e_type = e->event_type;


	if (e_type == SURVIVE_INPUT_EVENT_BUTTON_UP) {
		enum input_index index = buttons[btn_id].click;
		struct xrt_input *input = &survive->base.inputs[index];
		input->value.boolean = false;
		input->timestamp = now;
	} else if (e_type == SURVIVE_INPUT_EVENT_BUTTON_DOWN) {
		enum input_index index = buttons[btn_id].click;
		struct xrt_input *input = &survive->base.inputs[index];
		input->value.boolean = true;
		input->timestamp = now;
	} else if (e_type == SURVIVE_INPUT_EVENT_TOUCH_UP) {
		enum input_index index = buttons[btn_id].touch;
		struct xrt_input *input = &survive->base.inputs[index];
		input->value.boolean = false;
		input->timestamp = now;
	} else if (e_type == SURVIVE_INPUT_EVENT_TOUCH_DOWN) {
		enum input_index index = buttons[btn_id].touch;
		struct xrt_input *input = &survive->base.inputs[index];
		input->value.boolean = true;
		input->timestamp = now;
	}

	return true;
}

static float
_calculate_squeeze_value(struct survive_device *survive)
{
	/*! @todo find a good formula for squeeze value */
	float val = 0;
	val = fmaxf(val, survive->ctrl.curl[XRT_FINGER_LITTLE]);
	val = fmaxf(val, survive->ctrl.curl[XRT_FINGER_RING]);
	val = fmaxf(val, survive->ctrl.curl[XRT_FINGER_MIDDLE]);
	return val;
}

static void
_process_button_event(struct survive_device *survive, const struct SurviveSimpleButtonEvent *e, int64_t now)
{
	if (e->event_type == SURVIVE_INPUT_EVENT_AXIS_CHANGED) {
		for (int i = 0; i < e->axis_count; i++) {

			struct Axis *axis = &axes[e->axis_ids[i]];
			float val = e->axis_val[i];

			if (update_axis(survive, axis, e, i, now)) {


			} else if (e->axis_ids[i] == SURVIVE_AXIS_TRIGGER_FINGER_PROXIMITY) {
				survive->ctrl.curl[XRT_FINGER_INDEX] = val;
				survive->ctrl.curl_ts[XRT_FINGER_INDEX] = now;
			} else if (e->axis_ids[i] == SURVIVE_AXIS_MIDDLE_FINGER_PROXIMITY) {
				survive->ctrl.curl[XRT_FINGER_MIDDLE] = val;
				survive->ctrl.curl_ts[XRT_FINGER_MIDDLE] = now;
			} else if (e->axis_ids[i] == SURVIVE_AXIS_RING_FINGER_PROXIMITY) {
				survive->ctrl.curl[XRT_FINGER_RING] = val;
				survive->ctrl.curl_ts[XRT_FINGER_RING] = now;
			} else if (e->axis_ids[i] == SURVIVE_AXIS_PINKY_FINGER_PROXIMITY) {
				survive->ctrl.curl[XRT_FINGER_LITTLE] = val;
				survive->ctrl.curl_ts[XRT_FINGER_LITTLE] = now;
			} else {
				SURVIVE_DEBUG(survive, "axis id: %d val %f", e->axis_ids[i], e->axis_val[i]);
			}
		}
		struct xrt_input *squeeze_value_in = &survive->base.inputs[VIVE_CONTROLLER_SQUEEZE_VALUE];
		float prev_squeeze_value = squeeze_value_in->value.vec1.x;
		float squeeze_value = _calculate_squeeze_value(survive);
		if (prev_squeeze_value != squeeze_value) {
			squeeze_value_in->value.vec1.x = squeeze_value;
			squeeze_value_in->timestamp = now;
		}
	}

	update_button(survive, e, now);
}

static void
_process_hmd_button_event(struct survive_device *survive, const struct SurviveSimpleButtonEvent *e, int64_t now)
{
	if (e->event_type == SURVIVE_INPUT_EVENT_AXIS_CHANGED) {
		for (int i = 0; i < e->axis_count; i++) {
			float val = e->axis_val[i];

			if (e->axis_ids[i] == SURVIVE_AXIS_IPD) {
				float ipd = val;
				float range = INDEX_MAX_IPD - INDEX_MIN_IPD;
				ipd *= range;
				ipd += INDEX_MIN_IPD;
				survive->hmd.ipd = ipd;

				// SURVIVE_DEBUG(survive, "ipd: %f meter", ipd);
			} else if (e->axis_ids[i] == SURVIVE_AXIS_FACE_PROXIMITY) {
				// Valve Index:
				// >0.003 not wearing hmd
				//  0.03-0.035 wearing hmd
				const float threshold = 0.02;

				float proximity = val;

				// extreme closeup may overflow?
				if (proximity < 0) {
					proximity = 1.0;
				}

				float curr = survive->hmd.proximity;
				bool engagement = (curr <= threshold && proximity > threshold) ||
				                  (curr >= threshold && proximity < threshold);

				if (engagement) {
					//! @todo engagement changed
				}
				// SURVIVE_DEBUG(survive, "Proximity %f",
				// proximity);

				survive->hmd.proximity = proximity;
			} else {
				SURVIVE_DEBUG(survive, "axis id: %d val %f", e->axis_ids[i], e->axis_val[i]);
			}
		}
	}
}

static struct survive_device *
get_device_by_object(struct survive_system *sys, const SurviveSimpleObject *object)
{
	if (sys->hmd->survive_obj == object) {
		return sys->hmd;
	}

	for (int i = 0; i < CONTROLLER_COUNT; i++) {
		if (sys->controllers[i] == NULL) {
			continue;
		}

		if (sys->controllers[i]->survive_obj == object) {
			return sys->controllers[i];
		}
	}
	return NULL;
}

static void
_process_event(struct survive_device *survive, struct SurviveSimpleEvent *event, int64_t now)
{
	switch (event->event_type) {
	case SurviveSimpleEventType_ButtonEvent: {
		const struct SurviveSimpleButtonEvent *e = survive_simple_get_button_event(event);

		struct survive_device *event_device = survive;

		if (e->object != survive->survive_obj) {
			event_device = get_device_by_object(survive->sys, e->object);
		}

		if (event_device == NULL) {
			SURVIVE_ERROR(survive, "Event for unknown object not handled");
			return;
		}

		// hmd & controller axes have overlapping enum indices
		if (event_device == survive->sys->hmd) {
			_process_hmd_button_event(event_device, e, now);
		} else {
			_process_button_event(event_device, e, now);
		}

		break;
	}
	case SurviveSimpleEventType_None: break;
	default: SURVIVE_ERROR(survive, "Unknown event %d", event->event_type);
	}
}


static void
survive_device_update_inputs(struct xrt_device *xdev)
{
	struct survive_device *survive = (struct survive_device *)xdev;

	uint64_t now = os_monotonic_get_ns();

	/* one event queue for all devices. _process_events() updates all
	 devices, not just this survive device. */

	struct SurviveSimpleEvent event = {0};
	while (survive_simple_next_event(survive->sys->ctx, &event) != SurviveSimpleEventType_None) {
		_process_event(survive, &event, now);
	}
}

static bool
wait_for_device_config(const struct SurviveSimpleObject *sso)
{
	// if not backed by a survive object, we will never get a config
	if (!survive_has_obj(sso)) {
		return false;
	}

	double start = time_ns_to_s(os_monotonic_get_ns());
	do {
		if (survive_config_ready(sso)) {
			return true;
		}
		os_nanosleep(1000 * 1000 * 100);
	} while (time_ns_to_s(os_monotonic_get_ns()) - start < WAIT_TIMEOUT);

	return false;
}

void
print_vec3(const char *title, struct xrt_vec3 *vec)
{
	U_LOG_D("%s = %f %f %f", title, (double)vec->x, (double)vec->y, (double)vec->z);
}

static long long
_json_to_int(const cJSON *item)
{
	if (item != NULL) {
		return item->valueint;
	} else {
		return 0;
	}
}

static bool
_json_get_matrix_3x3(const cJSON *json, const char *name, struct xrt_matrix_3x3 *result)
{
	const cJSON *vec3_arr = cJSON_GetObjectItemCaseSensitive(json, name);

	// Some sanity checking.
	if (vec3_arr == NULL || cJSON_GetArraySize(vec3_arr) != 3) {
		return false;
	}

	size_t total = 0;
	const cJSON *vec = NULL;
	cJSON_ArrayForEach(vec, vec3_arr)
	{
		assert(cJSON_GetArraySize(vec) == 3);
		const cJSON *elem = NULL;
		cJSON_ArrayForEach(elem, vec)
		{
			// Just in case.
			if (total >= 9) {
				break;
			}

			assert(cJSON_IsNumber(elem));
			result->v[total++] = (float)elem->valuedouble;
		}
	}

	return true;
}

static float
_json_get_float(const cJSON *json, const char *name)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	return (float)item->valuedouble;
}

static long long
_json_get_int(const cJSON *json, const char *name)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
	return _json_to_int(item);
}

static void
_get_color_coeffs(struct u_vive_values *values, const cJSON *coeffs, uint8_t eye, uint8_t channel)
{
	// For Vive this is 8 with only 3 populated.
	// For Index this is 4 with all values populated.
	const cJSON *item = NULL;
	size_t i = 0;
	cJSON_ArrayForEach(item, coeffs)
	{
		values->coefficients[channel][i] = (float)item->valuedouble;
		++i;
		if (i == 4) {
			break;
		}
	}
}

static void
get_distortion_properties(struct survive_device *d, const cJSON *eye_transform_json, uint8_t eye)
{
	const cJSON *eye_json = cJSON_GetArrayItem(eye_transform_json, eye);
	if (eye_json == NULL) {
		return;
	}

	struct xrt_matrix_3x3 rot = {0};
	if (_json_get_matrix_3x3(eye_json, "eye_to_head", &rot)) {
		math_quat_from_matrix_3x3(&rot, &d->hmd.rot[eye]);
	}

	// TODO: store grow_for_undistort per eye
	// clang-format off
	d->distortion[eye].grow_for_undistort = _json_get_float(eye_json, "grow_for_undistort");
	d->distortion[eye].undistort_r2_cutoff = _json_get_float(eye_json, "undistort_r2_cutoff");
	// clang-format on

	const char *names[3] = {
	    "distortion_red",
	    "distortion",
	    "distortion_blue",
	};

	for (int i = 0; i < 3; i++) {
		const cJSON *distortion = cJSON_GetObjectItemCaseSensitive(eye_json, names[i]);
		if (distortion == NULL) {
			continue;
		}

		d->distortion[eye].center[i].x = _json_get_float(distortion, "center_x");
		d->distortion[eye].center[i].y = _json_get_float(distortion, "center_y");

		const cJSON *coeffs = cJSON_GetObjectItemCaseSensitive(distortion, "coeffs");
		if (coeffs != NULL) {
			_get_color_coeffs(&d->distortion[eye], coeffs, eye, i);
		}
	}
}

static bool
compute_distortion(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct survive_device *d = (struct survive_device *)xdev;
	return u_compute_distortion_vive(&d->distortion[view], u, v, result);
}

static bool
_create_hmd_device(struct survive_system *sys, enum VIVE_VARIANT variant, const SurviveSimpleObject *sso)
{
	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)U_DEVICE_ALLOC_HMD;
	int inputs = 1;
	int outputs = 0;

	struct survive_device *survive = U_DEVICE_ALLOCATE(struct survive_device, flags, inputs, outputs);
	sys->hmd = survive;
	survive->sys = sys;
	survive->survive_obj = sso;
	survive->variant = variant;

	survive->base.name = XRT_DEVICE_GENERIC_HMD;
	snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "Survive HMD");
	survive->base.destroy = survive_device_destroy;
	survive->base.update_inputs = survive_device_update_inputs;
	survive->base.get_tracked_pose = survive_device_get_tracked_pose;
	survive->base.get_view_pose = survive_device_get_view_pose;
	survive->base.tracking_origin = &sys->base;

	SURVIVE_INFO(survive, "survive HMD present");

	survive->base.hmd->blend_mode = XRT_BLEND_MODE_OPAQUE;

	char *json_string = survive_get_json_config(survive->survive_obj);
	cJSON *json = cJSON_Parse(json_string);
	if (!cJSON_IsObject(json)) {
		SURVIVE_ERROR(survive, "Could not parse JSON data.");
		return false;
	}


	// TODO: Replace hard coded values from OpenHMD with config
	double w_meters = 0.122822 / 2.0;
	double h_meters = 0.068234;
	double lens_horizontal_separation = 0.057863;
	double eye_to_screen_distance = 0.023226876441867737;
	if (survive->variant == VIVE_VARIANT_VALVE_INDEX) {
		lens_horizontal_separation = 0.06;
		h_meters = 0.07;
		// eye relief knob adjusts this around [0.0255(near)-0.275(far)]
		eye_to_screen_distance = 0.0255;
	}


	double fov = 2 * atan2(w_meters - lens_horizontal_separation / 2.0, eye_to_screen_distance);

	for (int view = 0; view < 2; view++) {
		survive->distortion[view].aspect_x_over_y = 0.89999997615814209f;
		survive->distortion[view].grow_for_undistort = 0.5f;
		survive->distortion[view].undistort_r2_cutoff = 1.0f;
	}

	survive->hmd.rot[0].w = 1.0f;
	survive->hmd.rot[1].w = 1.0f;

	//! @todo: use IPD for FOV
	survive->hmd.ipd = 0.063;
	survive->hmd.proximity = 0;

	uint16_t w_pixels = 1080;
	uint16_t h_pixels = 1200;
	const cJSON *device_json = cJSON_GetObjectItemCaseSensitive(json, "device");
	if (device_json) {
		if (survive->variant != VIVE_VARIANT_VALVE_INDEX) {
			survive->distortion[0].aspect_x_over_y =
			    _json_get_float(device_json, "physical_aspect_x_over_y");
			survive->distortion[1].aspect_x_over_y = survive->distortion[0].aspect_x_over_y;

			//! @todo: fov calculation needs to be fixed, only works
			//! with hardcoded value
			// lens_horizontal_separation = _json_get_double(json,
			// "lens_separation");
		}
		h_pixels = (uint16_t)_json_get_int(device_json, "eye_target_height_in_pixels");
		w_pixels = (uint16_t)_json_get_int(device_json, "eye_target_width_in_pixels");
	}

	const cJSON *eye_transform_json = cJSON_GetObjectItemCaseSensitive(json, "tracking_to_eye_transform");
	if (eye_transform_json) {
		for (uint8_t eye = 0; eye < 2; eye++) {
			get_distortion_properties(survive, eye_transform_json, eye);
		}
	}

	SURVIVE_INFO(survive, "Survive eye resolution %dx%d", w_pixels, h_pixels);

	cJSON_Delete(json);

	// Main display.
	survive->base.hmd->screens[0].w_pixels = (int)w_pixels * 2;
	survive->base.hmd->screens[0].h_pixels = (int)h_pixels;

	if (survive->variant == VIVE_VARIANT_VALVE_INDEX)
		survive->base.hmd->screens[0].nominal_frame_interval_ns = (uint64_t)time_s_to_ns(1.0f / 144.0f);
	else
		survive->base.hmd->screens[0].nominal_frame_interval_ns = (uint64_t)time_s_to_ns(1.0f / 90.0f);

	struct xrt_vec2 lens_center[2];

	for (uint8_t eye = 0; eye < 2; eye++) {
		struct xrt_view *v = &survive->base.hmd->views[eye];
		v->display.w_meters = (float)w_meters;
		v->display.h_meters = (float)h_meters;
		v->display.w_pixels = w_pixels;
		v->display.h_pixels = h_pixels;
		v->viewport.w_pixels = w_pixels;
		v->viewport.h_pixels = h_pixels;
		v->viewport.y_pixels = 0;
		lens_center[eye].y = (float)h_meters / 2.0f;
		v->rot = u_device_rotation_ident;
	}

	// Left
	lens_center[0].x = (float)(w_meters - lens_horizontal_separation / 2.0);
	survive->base.hmd->views[0].viewport.x_pixels = 0;

	// Right
	lens_center[1].x = (float)lens_horizontal_separation / 2.0f;
	survive->base.hmd->views[1].viewport.x_pixels = w_pixels;

	for (uint8_t eye = 0; eye < 2; eye++) {
		if (!math_compute_fovs(w_meters, (double)lens_center[eye].x, fov, h_meters, (double)lens_center[eye].y,
		                       0, &survive->base.hmd->views[eye].fov)) {
			SURVIVE_ERROR(survive, "Failed to compute the partial fields of view.");
			free(survive);
			return NULL;
		}
	}

	survive->base.hmd->distortion.models = XRT_DISTORTION_MODEL_COMPUTE;
	survive->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_COMPUTE;
	survive->base.compute_distortion = compute_distortion;

	survive->base.orientation_tracking_supported = true;
	survive->base.position_tracking_supported = true;
	survive->base.device_type = XRT_DEVICE_TYPE_HMD;

	survive->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	return true;
}

/*
 *
 * Bindings
 *
 */

static struct xrt_binding_input_pair simple_inputs_index[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_INDEX_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_INDEX_B_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_INDEX_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_INDEX_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs_index[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_INDEX_HAPTIC},
};

static struct xrt_binding_input_pair simple_inputs_vive[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_VIVE_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_VIVE_MENU_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_VIVE_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_VIVE_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs_vive[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_VIVE_HAPTIC},
};

static struct xrt_binding_profile binding_profiles_index[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs_index,
        .num_inputs = ARRAY_SIZE(simple_inputs_index),
        .outputs = simple_outputs_index,
        .num_outputs = ARRAY_SIZE(simple_outputs_index),
    },
};

static struct xrt_binding_profile binding_profiles_vive[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs_vive,
        .num_inputs = ARRAY_SIZE(simple_inputs_vive),
        .outputs = simple_outputs_vive,
        .num_outputs = ARRAY_SIZE(simple_outputs_vive),
    },
};

#define SET_WAND_INPUT(NAME, NAME2)                                                                                    \
	do {                                                                                                           \
		(survive->base.inputs[VIVE_CONTROLLER_##NAME].name = XRT_INPUT_VIVE_##NAME2);                          \
	} while (0)

#define SET_INDEX_INPUT(NAME, NAME2)                                                                                   \
	do {                                                                                                           \
		(survive->base.inputs[VIVE_CONTROLLER_##NAME].name = XRT_INPUT_INDEX_##NAME2);                         \
	} while (0)

static bool
_create_controller_device(struct survive_system *sys, const SurviveSimpleObject *sso, enum VIVE_VARIANT variant)
{

	enum u_device_alloc_flags flags = 0;

	int inputs = VIVE_CONTROLLER_MAX_INDEX;
	int outputs = 1;
	struct survive_device *survive = U_DEVICE_ALLOCATE(struct survive_device, flags, inputs, outputs);

	int idx = -1;
	if (variant == VIVE_VARIANT_HTC_VIVE_CONTROLLER) {
		if (sys->controllers[SURVIVE_LEFT_CONTROLLER] == NULL) {
			idx = SURVIVE_LEFT_CONTROLLER;
		} else if (sys->controllers[SURVIVE_RIGHT_CONTROLLER] == NULL) {
			idx = SURVIVE_RIGHT_CONTROLLER;
		} else {
			SURVIVE_ERROR(survive, "Only creating 2 controllers!");
			return false;
		}
	} else if (variant == VIVE_VARIANT_VALVE_INDEX_LEFT_CONTROLLER) {
		if (sys->controllers[SURVIVE_LEFT_CONTROLLER] == NULL) {
			idx = SURVIVE_LEFT_CONTROLLER;
		} else {
			SURVIVE_ERROR(survive, "Only creating 1 left controller!");
			return false;
		}
	} else if (variant == VIVE_VARIANT_VALVE_INDEX_RIGHT_CONTROLLER) {
		if (sys->controllers[SURVIVE_RIGHT_CONTROLLER] == NULL) {
			idx = SURVIVE_RIGHT_CONTROLLER;
		} else {
			SURVIVE_ERROR(survive, "Only creating 1 right controller!");
			return false;
		}
	}
	sys->controllers[idx] = survive;
	survive->sys = sys;
	survive->variant = variant;
	survive->survive_obj = sso;

	survive->num = idx;
	survive->base.tracking_origin = &sys->base;

	survive->base.destroy = survive_device_destroy;
	survive->base.update_inputs = survive_device_update_inputs;
	survive->base.get_tracked_pose = survive_device_get_tracked_pose;
	survive->base.set_output = survive_controller_device_set_output;

	//! @todo: May use Vive Wands + Index HMDs or Index Controllers + Vive
	//! HMD
	if (variant == VIVE_VARIANT_VALVE_INDEX_LEFT_CONTROLLER ||
	    variant == VIVE_VARIANT_VALVE_INDEX_RIGHT_CONTROLLER) {
		survive->base.name = XRT_DEVICE_INDEX_CONTROLLER;
		snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "Survive Valve Index Controller %d", idx);

		SET_INDEX_INPUT(SYSTEM_CLICK, SYSTEM_CLICK);
		SET_INDEX_INPUT(A_CLICK, A_CLICK);
		SET_INDEX_INPUT(B_CLICK, B_CLICK);
		SET_INDEX_INPUT(TRIGGER_CLICK, TRIGGER_CLICK);
		SET_INDEX_INPUT(TRIGGER_VALUE, TRIGGER_VALUE);
		SET_INDEX_INPUT(TRACKPAD, TRACKPAD);
		SET_INDEX_INPUT(TRACKPAD_TOUCH, TRACKPAD_TOUCH);
		SET_INDEX_INPUT(THUMBSTICK, THUMBSTICK);
		SET_INDEX_INPUT(THUMBSTICK_CLICK, THUMBSTICK_CLICK);

		SET_INDEX_INPUT(THUMBSTICK_TOUCH, THUMBSTICK_TOUCH);
		SET_INDEX_INPUT(SYSTEM_TOUCH, SYSTEM_TOUCH);
		SET_INDEX_INPUT(A_TOUCH, A_TOUCH);
		SET_INDEX_INPUT(B_TOUCH, B_TOUCH);
		SET_INDEX_INPUT(SQUEEZE_VALUE, SQUEEZE_VALUE);
		SET_INDEX_INPUT(SQUEEZE_FORCE, SQUEEZE_FORCE);
		SET_INDEX_INPUT(TRIGGER_TOUCH, TRIGGER_TOUCH);
		SET_INDEX_INPUT(TRACKPAD_FORCE, TRACKPAD_FORCE);

		SET_INDEX_INPUT(AIM_POSE, AIM_POSE);
		SET_INDEX_INPUT(GRIP_POSE, GRIP_POSE);

		if (variant == VIVE_VARIANT_VALVE_INDEX_LEFT_CONTROLLER) {
			survive->base.device_type = XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;
			survive->base.inputs[VIVE_CONTROLLER_HAND_TRACKING].name = XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
		} else if (variant == VIVE_VARIANT_VALVE_INDEX_RIGHT_CONTROLLER) {
			survive->base.device_type = XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER;
			survive->base.inputs[VIVE_CONTROLLER_HAND_TRACKING].name =
			    XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;
		} else {
			survive->base.device_type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER;
		}

		survive->base.get_hand_tracking = survive_controller_get_hand_tracking;

		enum xrt_hand hand = idx == SURVIVE_LEFT_CONTROLLER ? XRT_HAND_LEFT : XRT_HAND_RIGHT;
		u_hand_joints_init_default_set(&survive->ctrl.hand_tracking, hand, XRT_HAND_TRACKING_MODEL_FINGERL_CURL,
		                               1.0);

		survive->base.outputs[0].name = XRT_OUTPUT_NAME_INDEX_HAPTIC;

		survive->base.binding_profiles = binding_profiles_index;
		survive->base.num_binding_profiles = ARRAY_SIZE(binding_profiles_index);

		survive->base.hand_tracking_supported = true;

	} else if (survive->variant == VIVE_VARIANT_HTC_VIVE_CONTROLLER) {
		survive->base.name = XRT_DEVICE_VIVE_WAND;
		snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "Survive Vive Wand Controller %d", idx);

		SET_WAND_INPUT(SYSTEM_CLICK, SYSTEM_CLICK);
		SET_WAND_INPUT(SQUEEZE_CLICK, SQUEEZE_CLICK);
		SET_WAND_INPUT(MENU_CLICK, MENU_CLICK);
		SET_WAND_INPUT(TRIGGER_CLICK, TRIGGER_CLICK);
		SET_WAND_INPUT(TRIGGER_VALUE, TRIGGER_VALUE);
		SET_WAND_INPUT(TRACKPAD, TRACKPAD);
		SET_WAND_INPUT(TRACKPAD_CLICK, TRACKPAD_CLICK);
		SET_WAND_INPUT(TRACKPAD_TOUCH, TRACKPAD_TOUCH);

		SET_WAND_INPUT(AIM_POSE, AIM_POSE);
		SET_WAND_INPUT(GRIP_POSE, GRIP_POSE);

		survive->base.outputs[0].name = XRT_OUTPUT_NAME_VIVE_HAPTIC;

		survive->base.binding_profiles = binding_profiles_vive;
		survive->base.num_binding_profiles = ARRAY_SIZE(binding_profiles_vive);

		survive->base.device_type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER;
	}

	survive->base.orientation_tracking_supported = true;
	survive->base.position_tracking_supported = true;

	SURVIVE_DEBUG(survive, "Created Controller %d", idx);

	return true;
}

DEBUG_GET_ONCE_LOG_OPTION(survive_log, "SURVIVE_LOG", U_LOGGING_WARN)

static enum VIVE_VARIANT
_product_to_variant(uint16_t product_id)
{
	switch (product_id) {
	case VIVE_PID: return VIVE_VARIANT_VIVE;
	case VIVE_PRO_MAINBOARD_PID: return VIVE_VARIANT_PRO;
	case VIVE_PRO_LHR_PID: return VIVE_VARIANT_VALVE_INDEX;
	default: U_LOG_W("No product ids matched %.4x", product_id); return VIVE_UNKNOWN;
	}
}

#define JSON_STRING(a, b, c) u_json_get_string_into_array(u_json_get(a, b), c, sizeof(c))

static enum VIVE_VARIANT
get_variant_from_json(struct survive_system *ss, cJSON *json)
{
	char model_number[32];

	if (u_json_get(json, "model_number")) {
		JSON_STRING(json, "model_number", model_number);
	} else {
		JSON_STRING(json, "model_name", model_number);
	}

	enum VIVE_VARIANT variant = VIVE_UNKNOWN;

	if (strcmp(model_number, "Vive. Controller MV") == 0) {
		variant = VIVE_VARIANT_HTC_VIVE_CONTROLLER;
		U_LOG_D("Found Vive Wand controller");
	} else if (strcmp(model_number, "Knuckles Right") == 0) {
		variant = VIVE_VARIANT_VALVE_INDEX_RIGHT_CONTROLLER;
		U_LOG_D("Found Knuckles Right controller");
	} else if (strcmp(model_number, "Knuckles Left") == 0) {
		variant = VIVE_VARIANT_VALVE_INDEX_LEFT_CONTROLLER;
		U_LOG_D("Found Knuckles Left controller");
	} else if (strcmp(model_number, "Vive Tracker PVT") == 0) {
		variant = VIVE_VARIANT_TRACKER_V1;
		U_LOG_D("Found Gen 1 tracker.");
	} else if (strcmp(model_number, "VIVE Tracker Pro MV") == 0) {
		variant = VIVE_VARIANT_TRACKER_v2;
		U_LOG_D("Found Gen 2 tracker.");
	} else if (strcmp(model_number, "Utah MP") == 0) {
		U_LOG_W("Found Utah MP (Index HMD), not a controller!");
	} else {
		U_LOG_E("Failed to parse controller variant: %s", model_number);
	}

	return variant;
}

int
survive_found(struct xrt_prober *xp,
              struct xrt_prober_device **devices,
              size_t num_devices,
              size_t index,
              cJSON *attached_data,
              struct xrt_device **out_xdevs)
{
	if (survive_already_initialized) {
		U_LOG_I(
		    "Skipping libsurvive initialization, already "
		    "initialized");
		return 0;
	}

	SurviveSimpleContext *actx = NULL;
#if 1
	char *survive_args[] = {
	    "Monado-libsurvive",
	    //"--time-window", "1500000"
	    //"--use-imu", "0",
	    //"--use-kalman", "0"
	};
	actx = survive_simple_init(sizeof(survive_args) / sizeof(survive_args[0]), survive_args);
#else
	actx = survive_simple_init(0, 0);
#endif

	if (!actx) {
		U_LOG_E("failed to init survive");
		return false;
	}

	struct survive_system *ss = U_TYPED_CALLOC(struct survive_system);

	struct xrt_prober_device *dev = devices[index];

	survive_simple_start_thread(actx);

	ss->ctx = actx;
	ss->base.type = XRT_TRACKING_TYPE_LIGHTHOUSE;
	snprintf(ss->base.name, XRT_TRACKING_NAME_LEN, "%s", "Libsurvive Tracking");
	ss->base.offset.position.x = 0.0f;
	ss->base.offset.position.y = 0.0f;
	ss->base.offset.position.z = 0.0f;
	ss->base.offset.orientation.w = 1.0f;

	ss->ll = debug_get_log_option_survive_log();

	/* iterate over all devices, if SurviveSimpleObject_OBJECT parse config
	 * and if controller, add it with variant from config.
	 */
	for (const SurviveSimpleObject *it = survive_simple_get_first_object(ss->ctx); it != 0;
	     it = survive_simple_get_next_object(ss->ctx, it)) {

		if (!wait_for_device_config(it)) {
			U_LOG_IFL_E(ss->ll, "Failed to get device config from survive");
			continue;
		}

		U_LOG_IFL_D(ss->ll, "Got device config from survive");

		enum SurviveSimpleObject_type type = survive_simple_object_get_type(it);

		if (type == SurviveSimpleObject_HMD) {
			enum VIVE_VARIANT variant = _product_to_variant(dev->product_id);
			U_LOG_I("survive HMD: Assuming variant %d", variant);
			_create_hmd_device(ss, variant, it);
		} else if (type == SurviveSimpleObject_OBJECT) {
			char *json_string = survive_get_json_config(it);
			cJSON *json = cJSON_Parse(json_string);
			if (!cJSON_IsObject(json)) {
				U_LOG_IFL_E(ss->ll, "Could not parse JSON data.");
				cJSON_Delete(json);
				continue;
			}

			enum VIVE_VARIANT variant = get_variant_from_json(ss, json);

			switch (variant) {
			case VIVE_VARIANT_HTC_VIVE_CONTROLLER:
			case VIVE_VARIANT_VALVE_INDEX_LEFT_CONTROLLER:
			case VIVE_VARIANT_VALVE_INDEX_RIGHT_CONTROLLER:
				U_LOG_IFL_D(ss->ll, "Adding controller.");
				_create_controller_device(ss, it, variant);
				break;
			default:
				U_LOG_IFL_D(ss->ll, "Skip non controller obj.");
				U_LOG_IFL_T(ss->ll, "json: %s", json_string);
				break;
			}
			cJSON_Delete(json);
		} else {
			U_LOG_IFL_D(ss->ll, "Skip non OBJECT obj.");
		}
	}

	// U_LOG_D("Survive HMD %p, controller %p %p", (void *)ss->hmd,
	//        (void *)ss->controllers[0], (void *)ss->controllers[1]);

	if (ss->ll <= U_LOGGING_DEBUG) {
		if (ss->hmd) {
			u_device_dump_config(&ss->hmd->base, __func__, "libsurvive");
		}
	}

	int out_idx = 0;
	if (ss->hmd) {
		out_xdevs[out_idx++] = &ss->hmd->base;
	}
	if (&ss->controllers[SURVIVE_LEFT_CONTROLLER]) {
		out_xdevs[out_idx++] = &ss->controllers[SURVIVE_LEFT_CONTROLLER]->base;
	}
	if (&ss->controllers[SURVIVE_LEFT_CONTROLLER]) {
		out_xdevs[out_idx++] = &ss->controllers[SURVIVE_RIGHT_CONTROLLER]->base;
	}

	survive_already_initialized = true;
	return out_idx;
}
