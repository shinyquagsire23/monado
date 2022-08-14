// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Adapter to Libsurvive.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Moses Turner <moses@collabora.com>
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
#include "math/m_space.h"
#include "tracking/t_tracking.h"
#include "xrt/xrt_device.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_device.h"
#include "util/u_distortion_mesh.h"
#include "util/u_config_json.h"

#include "os/os_threading.h"

#include "os/os_time.h"

#include "xrt/xrt_prober.h"
#include "survive_interface.h"

#include "survive_api.h"

#include "util/u_json.h"

#include "util/u_hand_tracking.h"
#include "util/u_hand_simulation.h"
#include "util/u_logging.h"
#include "math/m_relation_history.h"

#include "math/m_predict.h"

#include "vive/vive_config.h"
#include "vive/vive_bindings.h"


#include "util/u_trace_marker.h"

// If we haven't gotten a config for devices this long after startup, just start without those devices
#define DEFAULT_WAIT_TIMEOUT 3.5f

// index in sys->controllers[] array
#define SURVIVE_LEFT_CONTROLLER_INDEX 0
#define SURVIVE_RIGHT_CONTROLLER_INDEX 1
#define SURVIVE_NON_CONTROLLER_START 2

//! excl HMD we support 16 devices (controllers, trackers, ...)
#define MAX_TRACKED_DEVICE_COUNT 16

DEBUG_GET_ONCE_BOOL_OPTION(survive_disable_hand_emulation, "SURVIVE_DISABLE_HAND_EMULATION", false)

#define SURVIVE_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->sys->log_level, __VA_ARGS__)
#define SURVIVE_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->sys->log_level, __VA_ARGS__)
#define SURVIVE_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->sys->log_level, __VA_ARGS__)
#define SURVIVE_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->sys->log_level, __VA_ARGS__)
#define SURVIVE_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->sys->log_level, __VA_ARGS__)

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

	VIVE_TRACKER_POSE,

	VIVE_CONTROLLER_MAX_INDEX,
};

enum DeviceType
{
	DEVICE_TYPE_HMD,
	DEVICE_TYPE_CONTROLLER
};

/*!
 * @implements xrt_device
 */
struct survive_device
{
	struct xrt_device base;
	struct survive_system *sys;
	const SurviveSimpleObject *survive_obj;

	struct m_relation_history *relation_hist;

	//! Number of inputs.
	size_t num_last_inputs;
	//! Array of input structs.
	struct xrt_input *last_inputs;

	enum DeviceType device_type;

	union {
		struct
		{
			float proximity; // [0,1]
			float ipd;

			struct vive_config config;
		} hmd;

		struct
		{
			float curl[XRT_FINGER_COUNT];
			uint64_t curl_ts[XRT_FINGER_COUNT];
			struct u_hand_tracking hand_tracking;

			struct vive_controller_config config;
		} ctrl;
	};
};

/*!
 * @extends xrt_tracking_origin
 */
struct survive_system
{
	struct xrt_tracking_origin base;
	SurviveSimpleContext *ctx;
	struct survive_device *hmd;
	struct survive_device *controllers[MAX_TRACKED_DEVICE_COUNT];
	enum u_logging_level log_level;

	float wait_timeout;

	struct os_thread_helper event_thread;
	struct os_mutex lock;
};

static void
survive_device_destroy(struct xrt_device *xdev)
{
	if (!xdev) {
		return;
	}

	U_LOG_D("destroying survive device");
	struct survive_device *survive = (struct survive_device *)xdev;

	if (survive == survive->sys->hmd) {
		vive_config_teardown(&survive->hmd.config);
		survive->sys->hmd = NULL;
	}
	for (int i = 0; i < MAX_TRACKED_DEVICE_COUNT; i++) {
		if (survive == survive->sys->controllers[i]) {
			survive->sys->controllers[i] = NULL;
		}
	}

	bool all_null = true;
	for (int i = 0; i < MAX_TRACKED_DEVICE_COUNT; i++) {
		if (survive->sys->controllers[i] != 0) {
			all_null = false;
		}
	}

	if (survive->sys->hmd == NULL && all_null) {
		U_LOG_D("Tearing down libsurvive context");

		// Destroy also stops the thread.
		os_thread_helper_destroy(&survive->sys->event_thread);

		// Now that the thread is not running we can destroy the lock.
		os_mutex_destroy(&survive->sys->lock);

		U_LOG_D("Stopped libsurvive event thread");

		survive_simple_close(survive->sys->ctx);
		free(survive->sys);
	}
	m_relation_history_destroy(&survive->relation_hist);

	free(survive->last_inputs);
	u_device_free(&survive->base);
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

static timepoint_ns
survive_timecode_to_monotonic(double timecode)
{
	timepoint_ns timecode_ns = time_s_to_ns(timecode);
	timepoint_ns survive_now_ns = time_s_to_ns(survive_timecode_now_s());

	timepoint_ns timecode_age_ns = survive_now_ns - timecode_ns;

	timepoint_ns now = os_monotonic_get_ns();
	timepoint_ns timestamp = now - timecode_age_ns;

	return timestamp;
}

static void
pose_to_relation(const SurvivePose *pose, const SurviveVelocity *vel, struct xrt_space_relation *out_relation)
{
	struct xrt_quat out_rot = {.x = pose->Rot[1], .y = pose->Rot[2], .z = pose->Rot[3], .w = pose->Rot[0]};

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
	out_relation->pose.position.x = pose->Pos[0];
	out_relation->pose.position.y = pose->Pos[2];
	out_relation->pose.position.z = -pose->Pos[1];

	struct xrt_vec3 linear_vel = {.x = vel->Pos[0], .y = vel->Pos[2], .z = -vel->Pos[1]};

	struct xrt_vec3 angular_vel = {
	    .x = vel->AxisAngleRot[0], .y = vel->AxisAngleRot[2], .z = -vel->AxisAngleRot[1]};

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
}

static bool
verify_device_name(struct survive_device *survive, enum xrt_input_name name)
{

	switch (survive->device_type) {
	case DEVICE_TYPE_HMD: return name == XRT_INPUT_GENERIC_HEAD_POSE;
	case DEVICE_TYPE_CONTROLLER:
		return name == XRT_INPUT_INDEX_AIM_POSE || name == XRT_INPUT_INDEX_GRIP_POSE ||
		       name == XRT_INPUT_VIVE_AIM_POSE || name == XRT_INPUT_VIVE_GRIP_POSE ||
		       name == XRT_INPUT_GENERIC_TRACKER_POSE;
	};
	return false;
}

static void
survive_device_get_tracked_pose(struct xrt_device *xdev,
                                enum xrt_input_name name,
                                uint64_t at_timestamp_ns,
                                struct xrt_space_relation *out_relation)
{
	struct survive_device *survive = (struct survive_device *)xdev;
	if (!verify_device_name(survive, name)) {
		SURVIVE_ERROR(survive, "unknown input name");
		return;
	}

	if (!survive->survive_obj) {
		// U_LOG_D("Obj not set for %p", (void*)survive);
		return;
	}

	m_relation_history_get(survive->relation_hist, at_timestamp_ns, out_relation);

	struct xrt_pose *p = &out_relation->pose;
	SURVIVE_TRACE(survive, "GET_POSITION (%f %f %f) GET_ORIENTATION (%f, %f, %f, %f)", p->position.x, p->position.y,
	              p->position.z, p->orientation.x, p->orientation.y, p->orientation.z, p->orientation.w);
}

static int
survive_controller_haptic_pulse(struct survive_device *survive, const union xrt_output_value *value)
{
	float duration_seconds;
	if (value->vibration.duration_ns == XRT_MIN_HAPTIC_DURATION) {
		SURVIVE_TRACE(survive, "Haptic pulse duration: using %f minimum", MIN_HAPTIC_DURATION);
		duration_seconds = MIN_HAPTIC_DURATION;
	} else {
		duration_seconds = time_ns_to_s(value->vibration.duration_ns);
	}

	float frequency = value->vibration.frequency;

	if (frequency == XRT_FREQUENCY_UNSPECIFIED) {
		SURVIVE_TRACE(survive, "Haptic pulse frequency unspecified, setting to %fHz", DEFAULT_HAPTIC_FREQ);
		frequency = DEFAULT_HAPTIC_FREQ;
	}

	float amplitude = value->vibration.amplitude;

	SURVIVE_TRACE(survive, "Got Haptic pulse amp %f, %fHz, %" PRId64 "ns", value->vibration.amplitude,
	              value->vibration.frequency, value->vibration.duration_ns);
	SURVIVE_TRACE(survive, "Doing Haptic pulse amp %f, %fHz, %fs", amplitude, frequency, duration_seconds);

	return survive_simple_object_haptic((struct SurviveSimpleObject *)survive->survive_obj, frequency, amplitude,
	                                    duration_seconds);
}

static void
survive_controller_device_set_output(struct xrt_device *xdev,
                                     enum xrt_output_name name,
                                     const union xrt_output_value *value)
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

static void
survive_controller_get_hand_tracking(struct xrt_device *xdev,
                                     enum xrt_input_name name,
                                     uint64_t at_timestamp_ns,
                                     struct xrt_hand_joint_set *out_value,
                                     uint64_t *out_timestamp_ns)
{
	struct survive_device *survive = (struct survive_device *)xdev;

	if (name != XRT_INPUT_GENERIC_HAND_TRACKING_LEFT && name != XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT) {
		SURVIVE_ERROR(survive, "unknown input name for hand tracker");
		return;
	}


	bool left = survive->ctrl.config.variant == CONTROLLER_INDEX_LEFT;
	enum xrt_hand hand = left ? XRT_HAND_LEFT : XRT_HAND_RIGHT;

	float thumb_curl = 0.0f;
	//! @todo place thumb preciely on the button that is touched/pressed
	if (survive->last_inputs[VIVE_CONTROLLER_A_TOUCH].value.boolean ||
	    survive->last_inputs[VIVE_CONTROLLER_B_TOUCH].value.boolean ||
	    survive->last_inputs[VIVE_CONTROLLER_THUMBSTICK_TOUCH].value.boolean ||
	    survive->last_inputs[VIVE_CONTROLLER_TRACKPAD_TOUCH].value.boolean) {
		thumb_curl = 1.0;
	}

	if (survive->last_inputs[buttons[SURVIVE_BUTTON_TRIGGER].click].value.boolean) {
		survive->ctrl.curl[XRT_FINGER_INDEX] = 1.0;
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
	struct xrt_vec3 static_offset = {.x = 0, .y = 0.05, .z = 0.11};



	struct xrt_space_relation hand_relation;

	m_relation_history_get(survive->relation_hist, at_timestamp_ns, &hand_relation);


	u_hand_sim_simulate_for_valve_index_knuckles(&values, hand, &hand_relation, out_value);

	struct xrt_pose hand_on_handle_pose;
	u_hand_joints_offset_valve_index_controller(hand, &static_offset, &hand_on_handle_pose);


	struct xrt_relation_chain chain = {0};

	// out_value->hand_pose = hand_relation;

	m_relation_chain_push_pose(&chain, &hand_on_handle_pose);
	m_relation_chain_push_relation(&chain, &hand_relation);
	m_relation_chain_resolve(&chain, &out_value->hand_pose);



	// This is the truth - we pose-predicted or interpolated all the way up to `at_timestamp_ns`.
	*out_timestamp_ns = at_timestamp_ns;

	// This is a lie - apparently libsurvive doesn't report controller tracked/untracked state, so just say that the
	// hand is being tracked
	out_value->is_active = true;
}

static void
survive_device_get_view_poses(struct xrt_device *xdev,
                              const struct xrt_vec3 *default_eye_relation,
                              uint64_t at_timestamp_ns,
                              uint32_t view_count,
                              struct xrt_space_relation *out_head_relation,
                              struct xrt_fov *out_fovs,
                              struct xrt_pose *out_poses)
{
	XRT_TRACE_MARKER();

	// Only supports two views.
	assert(view_count <= 2);

	u_device_get_view_poses(xdev, default_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
	                        out_poses);

	// This is for the Index' canted displays, on the Vive [Pro] they are identity.
	struct survive_device *survive = (struct survive_device *)xdev;
	for (uint32_t i = 0; i < view_count && i < ARRAY_SIZE(survive->hmd.config.display.rot); i++) {
		out_poses[i].orientation = survive->hmd.config.display.rot[i];
	}
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

static struct Axis axes[255] = {
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

	struct xrt_input *in = &survive->last_inputs[axis->input];

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



static bool
update_button(struct survive_device *survive, const struct SurviveSimpleButtonEvent *e, timepoint_ns ts)
{
	if (e->event_type == SURVIVE_INPUT_EVENT_NONE) {
		return true;
	}

	enum SurviveButton btn_id = e->button_id;
	enum SurviveInputEvent e_type = e->event_type;


	if (e_type == SURVIVE_INPUT_EVENT_BUTTON_UP) {
		enum input_index index = buttons[btn_id].click;
		struct xrt_input *input = &survive->last_inputs[index];
		input->value.boolean = false;
		input->timestamp = ts;
	} else if (e_type == SURVIVE_INPUT_EVENT_BUTTON_DOWN) {
		enum input_index index = buttons[btn_id].click;
		struct xrt_input *input = &survive->last_inputs[index];
		input->value.boolean = true;
		input->timestamp = ts;
	} else if (e_type == SURVIVE_INPUT_EVENT_TOUCH_UP) {
		enum input_index index = buttons[btn_id].touch;
		struct xrt_input *input = &survive->last_inputs[index];
		input->value.boolean = false;
		input->timestamp = ts;
	} else if (e_type == SURVIVE_INPUT_EVENT_TOUCH_DOWN) {
		enum input_index index = buttons[btn_id].touch;
		struct xrt_input *input = &survive->last_inputs[index];
		input->value.boolean = true;
		input->timestamp = ts;
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
_process_button_event(struct survive_device *survive, const struct SurviveSimpleButtonEvent *e)
{
	timepoint_ns ts = survive_timecode_to_monotonic(e->time);
	;
	if (e->event_type == SURVIVE_INPUT_EVENT_AXIS_CHANGED) {
		for (int i = 0; i < e->axis_count; i++) {

			struct Axis *axis = &axes[e->axis_ids[i]];
			float val = e->axis_val[i];

			if (update_axis(survive, axis, e, i, ts)) {


			} else if (e->axis_ids[i] == SURVIVE_AXIS_TRIGGER_FINGER_PROXIMITY) {
				survive->ctrl.curl[XRT_FINGER_INDEX] = val;
				survive->ctrl.curl_ts[XRT_FINGER_INDEX] = ts;
			} else if (e->axis_ids[i] == SURVIVE_AXIS_MIDDLE_FINGER_PROXIMITY) {
				survive->ctrl.curl[XRT_FINGER_MIDDLE] = val;
				survive->ctrl.curl_ts[XRT_FINGER_MIDDLE] = ts;
			} else if (e->axis_ids[i] == SURVIVE_AXIS_RING_FINGER_PROXIMITY) {
				survive->ctrl.curl[XRT_FINGER_RING] = val;
				survive->ctrl.curl_ts[XRT_FINGER_RING] = ts;
			} else if (e->axis_ids[i] == SURVIVE_AXIS_PINKY_FINGER_PROXIMITY) {
				survive->ctrl.curl[XRT_FINGER_LITTLE] = val;
				survive->ctrl.curl_ts[XRT_FINGER_LITTLE] = ts;
			} else {
				SURVIVE_DEBUG(survive, "axis id: %d val %f", e->axis_ids[i], e->axis_val[i]);
			}
		}
		struct xrt_input *squeeze_value_in = &survive->last_inputs[VIVE_CONTROLLER_SQUEEZE_VALUE];
		float prev_squeeze_value = squeeze_value_in->value.vec1.x;
		float squeeze_value = _calculate_squeeze_value(survive);
		if (prev_squeeze_value != squeeze_value) {
			squeeze_value_in->value.vec1.x = squeeze_value;
			squeeze_value_in->timestamp = ts;
		}
	}

	update_button(survive, e, ts);
}

static void
_process_hmd_button_event(struct survive_device *survive, const struct SurviveSimpleButtonEvent *e)
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
	if (sys->hmd != NULL && sys->hmd->survive_obj == object) {
		return sys->hmd;
	}

	for (int i = 0; i < MAX_TRACKED_DEVICE_COUNT; i++) {
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
add_device(struct survive_system *ss, const struct SurviveSimpleConfigEvent *e);

static void
_process_pose_event(struct survive_device *survive, const struct SurviveSimplePoseUpdatedEvent *e)
{
	struct xrt_space_relation rel;
	timepoint_ns ts;
	pose_to_relation(&e->pose, &e->velocity, &rel);
	ts = survive_timecode_to_monotonic(e->time);
	m_relation_history_push(survive->relation_hist, &rel, ts);

	SURVIVE_TRACE(survive, "Process pose event for %s", survive->base.str);
}

static void
_process_event(struct survive_system *ss, struct SurviveSimpleEvent *event)
{
	switch (event->event_type) {
	case SurviveSimpleEventType_ButtonEvent: {
		const struct SurviveSimpleButtonEvent *e = survive_simple_get_button_event(event);

		struct survive_device *event_device = get_device_by_object(ss, e->object);
		if (event_device == NULL) {
			U_LOG_IFL_I(ss->log_level, "Event for unknown object not handled");
			return;
		}

		// hmd & controller axes have overlapping enum indices
		if (event_device == ss->hmd) {
			_process_hmd_button_event(event_device, e);
		} else {
			_process_button_event(event_device, e);
		}

		break;
	}
	case SurviveSimpleEventType_ConfigEvent: {
		const struct SurviveSimpleConfigEvent *e = survive_simple_get_config_event(event);
		enum SurviveSimpleObject_type t = survive_simple_object_get_type(e->object);
		const char *name = survive_simple_object_name(e->object);
		U_LOG_IFL_D(ss->log_level, "Processing config for object name %s: type %d", name, t);
		add_device(ss, e);
		break;
	}
	case SurviveSimpleEventType_PoseUpdateEvent: {
		const struct SurviveSimplePoseUpdatedEvent *e = survive_simple_get_pose_updated_event(event);

		struct survive_device *event_device = get_device_by_object(ss, e->object);
		if (event_device == NULL) {
			U_LOG_IFL_E(ss->log_level, "Event for unknown object not handled");
			return;
		}

		_process_pose_event(event_device, e);
		break;
	}
	case SurviveSimpleEventType_DeviceAdded: {
		U_LOG_IFL_W(ss->log_level, "Device added event, but hotplugging not implemented yet");
		break;
	}
	case SurviveSimpleEventType_None: break;
	default: U_LOG_IFL_E(ss->log_level, "Unknown event %d", event->event_type);
	}
}

static void
survive_device_update_inputs(struct xrt_device *xdev)
{
	struct survive_device *survive = (struct survive_device *)xdev;

	os_mutex_lock(&survive->sys->lock);

	for (size_t i = 0; i < survive->base.input_count; i++) {
		survive->base.inputs[i] = survive->last_inputs[i];
	}

	os_mutex_unlock(&survive->sys->lock);
}

static bool
compute_distortion(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct survive_device *d = (struct survive_device *)xdev;
	return u_compute_distortion_vive(&d->hmd.config.distortion[view], u, v, result);
}

static bool
_create_hmd_device(struct survive_system *sys, const struct SurviveSimpleObject *sso, char *conf_str)
{

	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)U_DEVICE_ALLOC_HMD;
	int inputs = 1;
	int outputs = 0;

	struct survive_device *survive = U_DEVICE_ALLOCATE(struct survive_device, flags, inputs, outputs);

	if (!vive_config_parse(&survive->hmd.config, conf_str, sys->log_level)) {
		free(survive);
		return false;
	}

	sys->hmd = survive;
	survive->sys = sys;
	survive->survive_obj = sso;
	survive->device_type = DEVICE_TYPE_HMD;

	survive->base.name = XRT_DEVICE_GENERIC_HMD;
	survive->base.destroy = survive_device_destroy;
	survive->base.update_inputs = survive_device_update_inputs;
	survive->base.get_tracked_pose = survive_device_get_tracked_pose;
	survive->base.get_view_poses = survive_device_get_view_poses;
	survive->base.tracking_origin = &sys->base;

	SURVIVE_INFO(survive, "survive HMD present");
	m_relation_history_create(&survive->relation_hist);


	size_t idx = 0;
	survive->base.hmd->blend_modes[idx++] = XRT_BLEND_MODE_OPAQUE;
	survive->base.hmd->blend_mode_count = idx;

	switch (survive->hmd.config.variant) {
	case VIVE_VARIANT_VIVE: snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "HTC Vive (libsurvive)"); break;
	case VIVE_VARIANT_PRO: snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "HTC Vive Pro (libsurvive)"); break;
	case VIVE_VARIANT_INDEX: snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "Valve Index (libsurvive)"); break;
	case VIVE_UNKNOWN: snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "Unknown HMD (libsurvive)"); break;
	}
	snprintf(survive->base.serial, XRT_DEVICE_NAME_LEN, "%s", survive->hmd.config.firmware.device_serial_number);

	// TODO: Replace hard coded values from OpenHMD with config
	double w_meters = 0.122822 / 2.0;
	double h_meters = 0.068234;
	double lens_horizontal_separation = 0.057863;
	double eye_to_screen_distance = 0.023226876441867737;

	uint32_t w_pixels = survive->hmd.config.display.eye_target_width_in_pixels;
	uint32_t h_pixels = survive->hmd.config.display.eye_target_height_in_pixels;

	SURVIVE_DEBUG(survive, "display: %dx%d", w_pixels, h_pixels);

	// Main display.
	survive->base.hmd->screens[0].w_pixels = (int)w_pixels * 2;
	survive->base.hmd->screens[0].h_pixels = (int)h_pixels;

	if (survive->hmd.config.variant == VIVE_VARIANT_INDEX) {
		lens_horizontal_separation = 0.06;
		h_meters = 0.07;
		// eye relief knob adjusts this around [0.0255(near)-0.275(far)]
		eye_to_screen_distance = 0.0255;

		survive->base.hmd->screens[0].nominal_frame_interval_ns = (uint64_t)time_s_to_ns(1.0f / 144.0f);
	} else {
		survive->base.hmd->screens[0].nominal_frame_interval_ns = (uint64_t)time_s_to_ns(1.0f / 90.0f);
	}

	double fov = 2 * atan2(w_meters - lens_horizontal_separation / 2.0, eye_to_screen_distance);

	struct xrt_vec2 lens_center[2];

	for (uint8_t eye = 0; eye < 2; eye++) {
		struct xrt_view *v = &survive->base.hmd->views[eye];
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
		                       0, &survive->base.hmd->distortion.fov[eye])) {
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

	survive->last_inputs = U_TYPED_ARRAY_CALLOC(struct xrt_input, survive->base.input_count);
	survive->num_last_inputs = survive->base.input_count;
	for (size_t i = 0; i < survive->base.input_count; i++) {
		survive->last_inputs[i] = survive->base.inputs[i];
	}

	return true;
}

#define SET_WAND_INPUT(NAME, NAME2)                                                                                    \
	do {                                                                                                           \
		(survive->base.inputs[VIVE_CONTROLLER_##NAME].name = XRT_INPUT_VIVE_##NAME2);                          \
	} while (0)

#define SET_INDEX_INPUT(NAME, NAME2)                                                                                   \
	do {                                                                                                           \
		(survive->base.inputs[VIVE_CONTROLLER_##NAME].name = XRT_INPUT_INDEX_##NAME2);                         \
	} while (0)

static bool
_create_controller_device(struct survive_system *sys,
                          const SurviveSimpleObject *sso,
                          struct vive_controller_config *config)
{

	enum VIVE_CONTROLLER_VARIANT variant = config->variant;

	int idx = -1;
	if (variant == CONTROLLER_VIVE_WAND) {
		if (sys->controllers[SURVIVE_LEFT_CONTROLLER_INDEX] == NULL) {
			idx = SURVIVE_LEFT_CONTROLLER_INDEX;
		} else if (sys->controllers[SURVIVE_RIGHT_CONTROLLER_INDEX] == NULL) {
			idx = SURVIVE_RIGHT_CONTROLLER_INDEX;
		} else {
			U_LOG_IFL_E(sys->log_level, "Only creating 2 controllers!");
			return false;
		}
	} else if (variant == CONTROLLER_INDEX_LEFT) {
		if (sys->controllers[SURVIVE_LEFT_CONTROLLER_INDEX] == NULL) {
			idx = SURVIVE_LEFT_CONTROLLER_INDEX;
		} else {
			U_LOG_IFL_E(sys->log_level, "Only creating 1 left controller!");
			return false;
		}
	} else if (variant == CONTROLLER_INDEX_RIGHT) {
		if (sys->controllers[SURVIVE_RIGHT_CONTROLLER_INDEX] == NULL) {
			idx = SURVIVE_RIGHT_CONTROLLER_INDEX;
		} else {
			U_LOG_IFL_E(sys->log_level, "Only creating 1 right controller!");
			return false;
		}
	} else if (variant == CONTROLLER_TRACKER_GEN1 || variant == CONTROLLER_TRACKER_GEN2) {
		for (int i = SURVIVE_NON_CONTROLLER_START; i < MAX_TRACKED_DEVICE_COUNT; i++) {
			if (sys->controllers[i] == NULL) {
				idx = i;
				break;
			}
		}
	}

	if (idx == -1) {
		U_LOG_IFL_E(sys->log_level, "Skipping survive device we couldn't assign: %s!",
		            config->firmware.model_number);
		return false;
	}

	enum u_device_alloc_flags flags = 0;

	int inputs = VIVE_CONTROLLER_MAX_INDEX;
	int outputs = 1;
	struct survive_device *survive = U_DEVICE_ALLOCATE(struct survive_device, flags, inputs, outputs);
	survive->ctrl.config = *config;
	m_relation_history_create(&survive->relation_hist);

	sys->controllers[idx] = survive;
	survive->sys = sys;
	survive->survive_obj = sso;
	survive->device_type = DEVICE_TYPE_CONTROLLER;

	survive->base.tracking_origin = &sys->base;

	survive->base.destroy = survive_device_destroy;
	survive->base.update_inputs = survive_device_update_inputs;
	survive->base.get_tracked_pose = survive_device_get_tracked_pose;
	survive->base.set_output = survive_controller_device_set_output;
	snprintf(survive->base.serial, XRT_DEVICE_NAME_LEN, "%s", survive->ctrl.config.firmware.device_serial_number);

	if (variant == CONTROLLER_INDEX_LEFT || variant == CONTROLLER_INDEX_RIGHT) {
		survive->base.name = XRT_DEVICE_INDEX_CONTROLLER;

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

		if (variant == CONTROLLER_INDEX_LEFT) {
			survive->base.device_type = XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER;
			survive->base.inputs[VIVE_CONTROLLER_HAND_TRACKING].name = XRT_INPUT_GENERIC_HAND_TRACKING_LEFT;
			snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "Valve Index Left Controller (libsurvive)");
		} else if (variant == CONTROLLER_INDEX_RIGHT) {
			survive->base.device_type = XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER;
			survive->base.inputs[VIVE_CONTROLLER_HAND_TRACKING].name =
			    XRT_INPUT_GENERIC_HAND_TRACKING_RIGHT;
			snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "Valve Index Right Controller (libsurvive)");
		}

		survive->base.outputs[0].name = XRT_OUTPUT_NAME_INDEX_HAPTIC;

		survive->base.binding_profiles = vive_binding_profiles_index;
		survive->base.binding_profile_count = vive_binding_profiles_index_count;

		survive->base.get_hand_tracking = survive_controller_get_hand_tracking;
		survive->base.hand_tracking_supported = !debug_get_bool_option_survive_disable_hand_emulation();

	} else if (survive->ctrl.config.variant == CONTROLLER_VIVE_WAND) {
		survive->base.name = XRT_DEVICE_VIVE_WAND;
		snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "Vive Wand Controller (libsurvive)");

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

		survive->base.binding_profiles = vive_binding_profiles_wand;
		survive->base.binding_profile_count = vive_binding_profiles_wand_count;

		survive->base.device_type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER;
	} else if (survive->ctrl.config.variant == CONTROLLER_TRACKER_GEN1 ||
	           survive->ctrl.config.variant == CONTROLLER_TRACKER_GEN2) {
		if (survive->ctrl.config.variant == CONTROLLER_TRACKER_GEN1) {
			survive->base.name = XRT_DEVICE_VIVE_TRACKER_GEN1;
			snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "Vive Tracker Gen1 (libsurvive)");
		} else if (survive->ctrl.config.variant == CONTROLLER_TRACKER_GEN2) {
			survive->base.name = XRT_DEVICE_VIVE_TRACKER_GEN2;
			snprintf(survive->base.str, XRT_DEVICE_NAME_LEN, "Vive Tracker Gen2 (libsurvive)");
		}

		survive->base.device_type = XRT_DEVICE_TYPE_GENERIC_TRACKER;

		survive->base.inputs[VIVE_TRACKER_POSE].name = XRT_INPUT_GENERIC_TRACKER_POSE;
	}

	survive->base.orientation_tracking_supported = true;
	survive->base.position_tracking_supported = true;

	survive->last_inputs = U_TYPED_ARRAY_CALLOC(struct xrt_input, survive->base.input_count);
	survive->num_last_inputs = survive->base.input_count;
	for (size_t i = 0; i < survive->base.input_count; i++) {
		survive->last_inputs[i] = survive->base.inputs[i];
	}

	SURVIVE_DEBUG(survive, "Created Controller %d", idx);

	return true;
}

DEBUG_GET_ONCE_LOG_OPTION(survive_log, "SURVIVE_LOG", U_LOGGING_WARN)
DEBUG_GET_ONCE_OPTION(survive_lh_gen, "SURVIVE_LH_GEN", "0")

static void
add_device(struct survive_system *ss, const struct SurviveSimpleConfigEvent *e)
{
	struct SurviveSimpleObject *sso = e->object;

	U_LOG_IFL_D(ss->log_level, "Got device config from survive");

	enum SurviveSimpleObject_type type = survive_simple_object_get_type(sso);

	char *conf_str = (char *)survive_simple_json_config(sso);

	if (type == SurviveSimpleObject_HMD) {

		_create_hmd_device(ss, sso, conf_str);

	} else if (type == SurviveSimpleObject_OBJECT) {
		struct vive_controller_config config = {0};
		vive_config_parse_controller(&config, conf_str, ss->log_level);

		switch (config.variant) {
		case CONTROLLER_VIVE_WAND:
		case CONTROLLER_INDEX_LEFT:
		case CONTROLLER_INDEX_RIGHT:
		case CONTROLLER_TRACKER_GEN1:
		case CONTROLLER_TRACKER_GEN2:
			U_LOG_IFL_D(ss->log_level, "Adding controller: %s.", config.firmware.model_number);
			_create_controller_device(ss, sso, &config);
			break;
		default:
			U_LOG_IFL_D(ss->log_level, "Skip non controller obj %s.", config.firmware.model_number);
			U_LOG_IFL_T(ss->log_level, "json: %s", conf_str);
			break;
		}
	} else {
		U_LOG_IFL_D(ss->log_level, "Skip non OBJECT obj.");
	}
}

static bool
add_connected_devices(struct survive_system *ss)
{
	/** @todo We don't know how many device added events we will get.
	 * After 25ms Index HMD + Controllers are added here. So 250ms should be a safe value.
	 * Device added just means libsurvive knows the usb devices, the config will then be loaded asynchronously.
	 */
	os_nanosleep(250 * 1000 * 1000);

	size_t objs = survive_simple_get_object_count(ss->ctx);
	U_LOG_IFL_D(ss->log_level, "Object count: %zu", objs);

	timepoint_ns start = os_monotonic_get_ns();

	// First count how many non-lighthouse objects libsurvive knows.
	// Then poll events until we have gotten configs for this many, or until timeout.
	int configs_to_wait_for = 0;
	int configs_gotten = 0;

	for (const SurviveSimpleObject *sso = survive_simple_get_first_object(ss->ctx); sso;
	     sso = survive_simple_get_next_object(ss->ctx, sso)) {
		enum SurviveSimpleObject_type t = survive_simple_object_get_type(sso);
		const char *name = survive_simple_object_name(sso);
		U_LOG_IFL_D(ss->log_level, "Object name %s: type %d", name, t);

		// we only want to wait for configs of HMDs and controllers / trackers.
		// Note: HMDs will be of type SurviveSimpleObject_OBJECT until the config is loaded.
		if (t == SurviveSimpleObject_HMD || t == SurviveSimpleObject_OBJECT) {
			configs_to_wait_for++;
		}
	}

	U_LOG_IFL_D(ss->log_level, "Waiting for %d configs", configs_to_wait_for);
	while (configs_gotten < configs_to_wait_for) {
		struct SurviveSimpleEvent event = {0};
		while (survive_simple_next_event(ss->ctx, &event) != SurviveSimpleEventType_None) {
			if (event.event_type == SurviveSimpleEventType_ConfigEvent) {
				_process_event(ss, &event);
				configs_gotten++;
				U_LOG_IFL_D(ss->log_level, "Got config from device: %d/%d", configs_gotten,
				            configs_to_wait_for);
			} else {
				U_LOG_IFL_T(ss->log_level, "Skipping event type %d", event.event_type);
			}
		}

		if (time_ns_to_s(os_monotonic_get_ns() - start) > ss->wait_timeout) {
			U_LOG_IFL_D(ss->log_level, "Timed out after getting configs for %d/%d devices", configs_gotten,
			            configs_to_wait_for);
			break;
		}
		os_nanosleep(500 * 1000);
	}
	U_LOG_IFL_D(ss->log_level, "Waiting for configs took %f ms", time_ns_to_ms_f(os_monotonic_get_ns() - start));
	return true;
}

static void *
run_event_thread(void *ptr)
{
	struct survive_system *ss = (struct survive_system *)ptr;

	os_thread_helper_lock(&ss->event_thread);
	while (os_thread_helper_is_running_locked(&ss->event_thread)) {
		os_thread_helper_unlock(&ss->event_thread);

		// one event queue for all devices. _process_events() updates all devices
		struct SurviveSimpleEvent event = {0};
		survive_simple_wait_for_event(ss->ctx, &event);

		os_mutex_lock(&ss->lock);
		_process_event(ss, &event);
		os_mutex_unlock(&ss->lock);

		// Just keep swimming.
		os_thread_helper_lock(&ss->event_thread);
	}

	os_thread_helper_unlock(&ss->event_thread);

	return NULL;
}

int
survive_get_devices(struct xrt_device **out_xdevs, struct vive_config **out_vive_config)
{
	SurviveSimpleContext *actx = NULL;
#if 1
	char *survive_args[] = {
	    "Monado-libsurvive", "--lighthouse-gen", (char *)debug_get_option_survive_lh_gen(),
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
		return 0;
	}

	struct survive_system *ss = U_TYPED_CALLOC(struct survive_system);

	survive_simple_start_thread(actx);

	ss->ctx = actx;
	ss->base.type = XRT_TRACKING_TYPE_LIGHTHOUSE;
	snprintf(ss->base.name, XRT_TRACKING_NAME_LEN, "%s", "Libsurvive Tracking");
	ss->base.offset.position.x = 0.0f;
	ss->base.offset.position.y = 0.0f;
	ss->base.offset.position.z = 0.0f;
	ss->base.offset.orientation.w = 1.0f;

	ss->log_level = debug_get_log_option_survive_log();

	ss->wait_timeout = DEFAULT_WAIT_TIMEOUT;


	while (!add_connected_devices(ss)) {
		U_LOG_IFL_E(ss->log_level, "Failed to get device config from survive");
		continue;
	}

	if (ss->log_level <= U_LOGGING_DEBUG) {
		if (ss->hmd) {
			u_device_dump_config(&ss->hmd->base, __func__, "libsurvive");
		}
	}

	int out_idx = 0;

	if (ss->hmd != NULL) {
		out_xdevs[out_idx++] = &ss->hmd->base;
		*out_vive_config = &ss->hmd->hmd.config;
	}

	for (int i = 0; i < MAX_TRACKED_DEVICE_COUNT; i++) {

		if (out_idx == XRT_MAX_DEVICES_PER_PROBE - 1) {
			U_LOG_IFL_W(ss->log_level, "Probed max of %d devices, ignoring further devices",
			            XRT_MAX_DEVICES_PER_PROBE);
			return out_idx;
		}

		if (ss->controllers[i] != NULL) {
			out_xdevs[out_idx++] = &ss->controllers[i]->base;
		}
	}

	// Mutex before thread.
	int ret = os_mutex_init(&ss->lock);
	if (ret != 0) {
		U_LOG_IFL_E(ss->log_level, "Failed to init mutex!");
		survive_device_destroy((struct xrt_device *)ss->hmd);
		for (int i = 0; i < MAX_TRACKED_DEVICE_COUNT; i++) {
			survive_device_destroy((struct xrt_device *)ss->controllers[i]);
		}
		return 0;
	}

	os_thread_helper_init(&ss->event_thread);
	ret = os_thread_helper_start(&ss->event_thread, run_event_thread, ss);
	if (ret != 0) {
		U_LOG_IFL_E(ss->log_level, "Failed to start event thread!");
		survive_device_destroy((struct xrt_device *)ss->hmd);
		for (int i = 0; i < MAX_TRACKED_DEVICE_COUNT; i++) {
			survive_device_destroy((struct xrt_device *)ss->controllers[i]);
		}
		return 0;
	}
	return out_idx;
}
