// Copyright 2019, Collabora, Ltd.
// Copyright 2011, Iowa State University
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Razer Hydra prober and driver code
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * Portions based on the VRPN Razer Hydra driver,
 * originally written by Ryan Pavlik and available under the BSL-1.0.
 */


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xrt/xrt_prober.h"

#include "os/os_hid.h"
#include "os/os_time.h"

#include "math/m_api.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_logging.h"

#include "hydra_interface.h"



/*
 *
 * Defines & structs.
 *
 */

#define HYDRA_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->sys->ll, __VA_ARGS__)
#define HYDRA_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->sys->ll, __VA_ARGS__)
#define HYDRA_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->sys->ll, __VA_ARGS__)
#define HYDRA_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->sys->ll, __VA_ARGS__)
#define HYDRA_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->sys->ll, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(hydra_log, "HYDRA_LOG", U_LOGGING_WARN)

enum hydra_input_index
{
	HYDRA_INDEX_1_CLICK,
	HYDRA_INDEX_2_CLICK,
	HYDRA_INDEX_3_CLICK,
	HYDRA_INDEX_4_CLICK,
	HYDRA_INDEX_MIDDLE_CLICK,
	HYDRA_INDEX_BUMPER_CLICK,
	HYDRA_INDEX_JOYSTICK_CLICK,
	HYDRA_INDEX_JOYSTICK_VALUE,
	HYDRA_INDEX_TRIGGER_VALUE,
	HYDRA_INDEX_POSE,
	HYDRA_MAX_CONTROLLER_INDEX
};

/* Yes this is a bizarre bit mask. Mysteries of the Hydra. */
enum hydra_button_bit
{
	HYDRA_BUTTON_BIT_BUMPER = (1 << 0),

	HYDRA_BUTTON_BIT_3 = (1 << 1),
	HYDRA_BUTTON_BIT_1 = (1 << 2),
	HYDRA_BUTTON_BIT_2 = (1 << 3),
	HYDRA_BUTTON_BIT_4 = (1 << 4),

	HYDRA_BUTTON_BIT_MIDDLE = (1 << 5),
	HYDRA_BUTTON_BIT_JOYSTICK = (1 << 6),
};

static const uint8_t HYDRA_REPORT_START_MOTION[] = {

    0x00, // first byte must be report type
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00};

static const uint8_t HYDRA_REPORT_START_GAMEPAD[] = {
    0x00, // first byte must be report type
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00};

struct hydra_controller_state
{
	struct xrt_pose pose;
	struct xrt_vec2 js;
	float trigger;
	uint8_t buttons;
};
/*!
 * The states of the finite-state machine controlling the Hydra.
 */
enum hydra_sm_state
{
	HYDRA_SM_LISTENING_AFTER_CONNECT = 0,
	HYDRA_SM_LISTENING_AFTER_SET_FEATURE,
	HYDRA_SM_REPORTING
};

/*!
 * The details of the Hydra state machine in a convenient package.
 */
struct hydra_state_machine
{
	enum hydra_sm_state current_state;

	//! Time of the last (non-trivial) state transition
	timepoint_ns transition_time;
};

struct hydra_device;

/*!
 * A Razer Hydra system containing two controllers.
 *
 * @ingroup drv_hydra
 * @extends xrt_tracking_origin
 */
struct hydra_system
{
	struct xrt_tracking_origin base;
	struct os_hid_device *data_hid;
	struct os_hid_device *command_hid;

	struct hydra_state_machine sm;
	struct hydra_device *devs[2];

	int16_t report_counter;

	//! Last time that we received a report
	timepoint_ns report_time;

	/*!
	 * Reference count of the number of devices still alive using this
	 * system
	 */
	uint8_t refs;

	/*!
	 * Was the hydra in gamepad mode at start?
	 *
	 * If it was, we set it back to gamepad on destruction.
	 */
	bool was_in_gamepad_mode;
	int motion_attempt_number;

	enum u_logging_level ll;
};

/*!
 * A Razer Hydra device, representing just a single controller.
 *
 * @ingroup drv_hydra
 * @implements xrt_device
 */
struct hydra_device
{
	struct xrt_device base;
	struct hydra_system *sys;


	//! Last time that we updated inputs
	timepoint_ns input_time;

	// bool calibration_done;
	// int mirror;
	// int sign_x;

	struct hydra_controller_state state;

	//! Which hydra controller in the system are we?
	size_t index;
};

/*
 *
 * Internal functions.
 *
 */

static void
hydra_device_parse_controller(struct hydra_device *hd, uint8_t *buf);

static inline struct hydra_device *
hydra_device(struct xrt_device *xdev)
{
	assert(xdev);
	struct hydra_device *ret = (struct hydra_device *)xdev;
	assert(ret->sys != NULL);
	return ret;
}

static inline struct hydra_system *
hydra_system(struct xrt_tracking_origin *xtrack)
{
	assert(xtrack);
	struct hydra_system *ret = (struct hydra_system *)xtrack;
	return ret;
}

/*!
 * Reports the number of seconds since the most recent change of state.
 *
 * @relates hydra_sm
 */
static float
hydra_sm_seconds_since_transition(struct hydra_state_machine *hsm, timepoint_ns now)
{

	if (hsm->transition_time == 0) {
		hsm->transition_time = now;
		return 0.f;
	}

	float state_duration_s = time_ns_to_s(now - hsm->transition_time);
	return state_duration_s;
}
/*!
 * Performs a state transition, updating the transition time if the state
 * actually changed.
 *
 * @relates hydra_sm
 */
static int
hydra_sm_transition(struct hydra_state_machine *hsm, enum hydra_sm_state new_state, timepoint_ns now)
{
	if (hsm->transition_time == 0) {
		hsm->transition_time = now;
	}
	if (new_state != hsm->current_state) {
		hsm->current_state = new_state;
		hsm->transition_time = now;
	}
	return 0;
}
static inline uint8_t
hydra_read_uint8(uint8_t **bufptr)
{
	uint8_t ret = **bufptr;
	(*bufptr)++;
	return ret;
}
static inline int16_t
hydra_read_int16_le(uint8_t **bufptr)
{
	uint8_t *buf = *bufptr;
	//! @todo nothing actually defines XRT_BIG_ENDIAN when needed!
#ifdef XRT_BIG_ENDIAN
	uint8_t bytes[2] = {buf[1], buf[0]};
#else
	uint8_t bytes[2] = {buf[0], buf[1]};
#endif // XRT_BIG_ENDIAN
	(*bufptr) += 2;
	int16_t ret;
	memcpy(&ret, bytes, sizeof(ret));
	return ret;
}

/*!
 * Parse the controller-specific part of a buffer into a hydra device.
 */
static void
hydra_device_parse_controller(struct hydra_device *hd, uint8_t *buf)
{
	struct hydra_controller_state *state = &hd->state;
	static const float SCALE_MM_TO_METER = 0.001f;
	static const float SCALE_INT16_TO_FLOAT_PLUSMINUS_1 = 1.0f / 32768.0f;
	static const float SCALE_UINT8_TO_FLOAT_0_TO_1 = 1.0f / 255.0f;
	state->pose.position.x = hydra_read_int16_le(&buf) * SCALE_MM_TO_METER;
	state->pose.position.z = hydra_read_int16_le(&buf) * SCALE_MM_TO_METER;
	state->pose.position.y = -hydra_read_int16_le(&buf) * SCALE_MM_TO_METER;

	// the negatives are to fix handedness
	state->pose.orientation.w = hydra_read_int16_le(&buf) * SCALE_INT16_TO_FLOAT_PLUSMINUS_1;
	state->pose.orientation.x = hydra_read_int16_le(&buf) * SCALE_INT16_TO_FLOAT_PLUSMINUS_1;
	state->pose.orientation.y = -hydra_read_int16_le(&buf) * SCALE_INT16_TO_FLOAT_PLUSMINUS_1;
	state->pose.orientation.z = -hydra_read_int16_le(&buf) * SCALE_INT16_TO_FLOAT_PLUSMINUS_1;

	//! @todo the presence of this suggest we're not decoding the
	//! orientation right.
	math_quat_normalize(&state->pose.orientation);

	state->buttons = hydra_read_uint8(&buf);

	state->js.x = hydra_read_int16_le(&buf) * SCALE_INT16_TO_FLOAT_PLUSMINUS_1;
	state->js.y = hydra_read_int16_le(&buf) * SCALE_INT16_TO_FLOAT_PLUSMINUS_1;

	state->trigger = hydra_read_uint8(&buf) * SCALE_UINT8_TO_FLOAT_0_TO_1;

	HYDRA_TRACE(hd,
	            "\n\t"
	            "controller:  %i\n\t"
	            "position:    (%-1.2f, %-1.2f, %-1.2f)\n\t"
	            "orientation: (%-1.2f, %-1.2f, %-1.2f, %-1.2f)\n\t"
	            "buttons:     %08x\n\t"
	            "joystick:    (%-1.2f, %-1.2f)\n\t"
	            "trigger:     %01.2f\n",
	            (int)hd->index, state->pose.position.x, state->pose.position.y, state->pose.position.z,
	            state->pose.orientation.x, state->pose.orientation.y, state->pose.orientation.z,
	            state->pose.orientation.w, state->buttons, state->js.x, state->js.y, state->trigger);
}

static int
hydra_system_read_data_hid(struct hydra_system *hs, timepoint_ns now)
{
	assert(hs);
	uint8_t buffer[128];
	bool got_message = false;
	do {
		int ret = os_hid_read(hs->data_hid, buffer, sizeof(buffer), 0);
		if (ret < 0) {
			return ret;
		}
		if (ret == 0) {
			return got_message ? 1 : 0;
		}
		if (ret != 52) {
			U_LOG_IFL_E(hs->ll, "Unexpected data report of size %d", ret);
			return -1;
		}
		got_message = true;
		uint8_t new_counter = buffer[7];
		bool missed = false;
		if (hs->report_counter != -1) {
			uint8_t expected_counter = ((hs->report_counter + 1) & 0xff);
			missed = new_counter != expected_counter;
		}
		hs->report_counter = new_counter;

		if (hs->devs[0] != NULL) {
			hydra_device_parse_controller(hs->devs[0], buffer + 8);
		}
		if (hs->devs[1] != NULL) {
			hydra_device_parse_controller(hs->devs[1], buffer + 30);
		}

		hs->report_time = now;
		U_LOG_IFL_T(hs->ll,
		            "\n\t"
		            "missed: %s\n\t"
		            "seq_no: %x\n",
		            missed ? "yes" : "no", new_counter);
	} while (true);

	return 0;
}


/*!
 * Switch to motion controller mode.
 */
static int
hydra_system_enter_motion_control(struct hydra_system *hs, timepoint_ns now)
{
	assert(hs);

	hs->was_in_gamepad_mode = true;
	hs->motion_attempt_number++;
	U_LOG_IFL_D(hs->ll,
	            "Setting feature report to start motion-controller mode, "
	            "attempt %d",
	            hs->motion_attempt_number);

	os_hid_set_feature(hs->command_hid, HYDRA_REPORT_START_MOTION, sizeof(HYDRA_REPORT_START_MOTION));

	// Doing a dummy get-feature now.
	uint8_t buf[91] = {0};
	os_hid_get_feature(hs->command_hid, 0, buf, sizeof(buf));

	return hydra_sm_transition(&hs->sm, HYDRA_SM_LISTENING_AFTER_SET_FEATURE, now);
}
/*!
 * Update the internal state of the Hydra driver.
 *
 * Reads devices, checks the state machine and timeouts, etc.
 *
 */
static int
hydra_system_update(struct hydra_system *hs)
{
	assert(hs);
	timepoint_ns now = os_monotonic_get_ns();

	// In all states of the state machine:
	// Try reading a report: will only return >0 if we get a full motion
	// report.
	int received = hydra_system_read_data_hid(hs, now);

	if (received > 0) {
		return hydra_sm_transition(&hs->sm, HYDRA_SM_REPORTING, now);
	}


	switch (hs->sm.current_state) {

	case HYDRA_SM_LISTENING_AFTER_CONNECT: {
		float state_duration_s = hydra_sm_seconds_since_transition(&hs->sm, now);
		if (state_duration_s > 1.0f) {
			// only waiting 1 second for the initial report after
			// connect
			hydra_system_enter_motion_control(hs, now);
		}
	} break;

	case HYDRA_SM_LISTENING_AFTER_SET_FEATURE: {
		float state_duration_s = hydra_sm_seconds_since_transition(&hs->sm, now);
		if (state_duration_s > 5.0f) {
			// giving each motion control attempt 5 seconds to work.
			hydra_system_enter_motion_control(hs, now);
		}
	} break;

	default: break;
	}

	return 0;
}

static void
hydra_device_update_input_click(struct hydra_device *hd, timepoint_ns now, int index, uint32_t bit)
{
	assert(hd);
	hd->base.inputs[index].timestamp = now;
	hd->base.inputs[index].value.boolean = (hd->state.buttons & bit) != 0;
}

/*
 *
 * Device functions.
 *
 */

static void
hydra_device_update_inputs(struct xrt_device *xdev)
{
	struct hydra_device *hd = hydra_device(xdev);
	struct hydra_system *hs = hydra_system(xdev->tracking_origin);

	hydra_system_update(hs);

	if (hd->input_time != hs->report_time) {
		timepoint_ns now = hs->report_time;
		hd->input_time = now;

		hydra_device_update_input_click(hd, now, HYDRA_INDEX_1_CLICK, HYDRA_BUTTON_BIT_1);
		hydra_device_update_input_click(hd, now, HYDRA_INDEX_2_CLICK, HYDRA_BUTTON_BIT_2);
		hydra_device_update_input_click(hd, now, HYDRA_INDEX_3_CLICK, HYDRA_BUTTON_BIT_3);
		hydra_device_update_input_click(hd, now, HYDRA_INDEX_4_CLICK, HYDRA_BUTTON_BIT_4);

		hydra_device_update_input_click(hd, now, HYDRA_INDEX_MIDDLE_CLICK, HYDRA_BUTTON_BIT_MIDDLE);
		hydra_device_update_input_click(hd, now, HYDRA_INDEX_BUMPER_CLICK, HYDRA_BUTTON_BIT_BUMPER);
		hydra_device_update_input_click(hd, now, HYDRA_INDEX_JOYSTICK_CLICK, HYDRA_INDEX_JOYSTICK_CLICK);

		struct xrt_input *inputs = hd->base.inputs;
		struct hydra_controller_state *state = &(hd->state);

		inputs[HYDRA_INDEX_JOYSTICK_VALUE].timestamp = now;
		inputs[HYDRA_INDEX_JOYSTICK_VALUE].value.vec2 = state->js;

		inputs[HYDRA_INDEX_TRIGGER_VALUE].timestamp = now;
		inputs[HYDRA_INDEX_TRIGGER_VALUE].value.vec1.x = state->trigger;


		//! @todo report pose
		// inputs[HYDRA_INDEX_POSE].timestamp = now;
		// inputs[HYDRA_INDEX_POSE].value.
	}
}

static void
hydra_device_get_tracked_pose(struct xrt_device *xdev,
                              enum xrt_input_name name,
                              uint64_t at_timestamp_ns,
                              struct xrt_space_relation *out_relation)
{
	struct hydra_device *hd = hydra_device(xdev);
	struct hydra_system *hs = hydra_system(xdev->tracking_origin);

	hydra_system_update(hs);

	out_relation->pose = hd->state.pose;

	//! @todo how do we report this is not (necessarily) the same base space
	//! as the HMD?
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
	// struct xrt_vec3 pos = out_relation->pose.position;
	// struct xrt_quat quat = out_relation->pose.orientation;
	// HYDRA_SPEW(hd, "GET_TRACKED_POSE (%f, %f, %f) (%f, %f, %f, %f) ",
	// pos.x,
	//            pos.y, pos.z, quat.x, quat.y, quat.z, quat.w);
}

static void
hydra_system_remove_child(struct hydra_system *hs, struct hydra_device *hd)
{
	assert(hydra_system(hd->base.tracking_origin) == hs);
	assert(hd->index == 0 || hd->index == 1);

	// Make the device not point to the system
	hd->sys = NULL;

	// Make the system not point to the device
	assert(hs->devs[hd->index] == hd);
	hs->devs[hd->index] = NULL;

	// Decrease ref count of system
	hs->refs--;

	if (hs->refs == 0) {
		// No more children, destroy system.
		if (hs->data_hid != NULL && hs->command_hid != NULL && hs->sm.current_state == HYDRA_SM_REPORTING &&
		    hs->was_in_gamepad_mode) {

			U_LOG_IFL_D(hs->ll,
			            "hydra: Sending command to re-enter gamepad mode "
			            "and pausing while it takes effect.");

			os_hid_set_feature(hs->command_hid, HYDRA_REPORT_START_GAMEPAD,
			                   sizeof(HYDRA_REPORT_START_GAMEPAD));
			os_nanosleep(2 * 1000 * 1000 * 1000);
		}
		if (hs->data_hid != NULL) {
			os_hid_destroy(hs->data_hid);
			hs->data_hid = NULL;
		}
		if (hs->command_hid != NULL) {
			os_hid_destroy(hs->command_hid);
			hs->command_hid = NULL;
		}
		free(hs);
	}
}

static void
hydra_device_destroy(struct xrt_device *xdev)
{
	struct hydra_device *hd = hydra_device(xdev);
	struct hydra_system *hs = hydra_system(xdev->tracking_origin);

	hydra_system_remove_child(hs, hd);

	free(hd);
}

/*
 *
 * Prober functions.
 *
 */
#define SET_INPUT(NAME)                                                                                                \
	do {                                                                                                           \
		(hd->base.inputs[HYDRA_INDEX_##NAME].name = XRT_INPUT_HYDRA_##NAME);                                   \
	} while (0)

int
hydra_found(struct xrt_prober *xp,
            struct xrt_prober_device **devices,
            size_t num_devices,
            size_t index,
            cJSON *attached_data,
            struct xrt_device **out_xdevs)
{
	struct xrt_prober_device *dev = devices[index];
	int ret;

	struct os_hid_device *data_hid = NULL;
	ret = xp->open_hid_interface(xp, dev, 0, &data_hid);
	if (ret != 0) {
		return -1;
	}
	struct os_hid_device *command_hid = NULL;
	ret = xp->open_hid_interface(xp, dev, 1, &command_hid);
	if (ret != 0) {
		data_hid->destroy(data_hid);
		return -1;
	}

	// Create the system
	struct hydra_system *hs = U_TYPED_CALLOC(struct hydra_system);
	hs->base.type = XRT_TRACKING_TYPE_HYDRA;
	snprintf(hs->base.name, XRT_TRACKING_NAME_LEN, "%s", "Razer Hydra magnetic tracking");
	// Dummy transform from local space to base.
	hs->base.offset.position.y = 1.0f;
	hs->base.offset.position.z = -0.25f;
	hs->base.offset.orientation.w = 1.0f;

	hs->data_hid = data_hid;
	hs->command_hid = command_hid;

	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)0;
	hs->devs[0] = U_DEVICE_ALLOCATE(struct hydra_device, flags, 10, 0);
	hs->devs[1] = U_DEVICE_ALLOCATE(struct hydra_device, flags, 10, 0);

	// Populate the "tracking" member with the system.
	hs->devs[0]->base.tracking_origin = &(hs->base);
	hs->devs[1]->base.tracking_origin = &(hs->base);

	hs->report_counter = -1;
	hs->refs = 2;

	hs->ll = debug_get_log_option_hydra_log();

	// Populate the individual devices
	for (size_t i = 0; i < 2; ++i) {
		struct hydra_device *hd = hs->devs[i];

		hd->base.destroy = hydra_device_destroy;
		hd->base.update_inputs = hydra_device_update_inputs;
		hd->base.get_tracked_pose = hydra_device_get_tracked_pose;
		// hs->base.set_output = hydra_device_set_output;
		hd->base.name = XRT_DEVICE_HYDRA;
		snprintf(hd->base.str, XRT_DEVICE_NAME_LEN, "%s %i", "Razer Hydra Controller", (int)(i + 1));
		SET_INPUT(1_CLICK);
		SET_INPUT(2_CLICK);
		SET_INPUT(3_CLICK);
		SET_INPUT(4_CLICK);
		SET_INPUT(MIDDLE_CLICK);
		SET_INPUT(BUMPER_CLICK);
		SET_INPUT(JOYSTICK_CLICK);
		SET_INPUT(JOYSTICK_VALUE);
		SET_INPUT(TRIGGER_VALUE);
		SET_INPUT(POSE);
		hd->index = i;
		hd->sys = hs;

		out_xdevs[i] = &(hd->base);
	}

	hs->devs[0]->base.orientation_tracking_supported = true;
	hs->devs[0]->base.device_type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER;
	hs->devs[1]->base.position_tracking_supported = true;
	hs->devs[1]->base.device_type = XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER;

	U_LOG_I("Opened razer hydra!");
	return 2;
}
