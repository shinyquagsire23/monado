// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Driver for Bluetooth based WMR Controller.
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */

#include "os/os_time.h"
#include "os/os_hid.h"

#include "math/m_mathinclude.h"
#include "math/m_api.h"
#include "math/m_vec2.h"
#include "math/m_predict.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_device.h"

#include "wmr_common.h"
#include "wmr_bt_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifndef XRT_OS_WINDOWS
#include <unistd.h> // for sleep()
#endif

#define WMR_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->log_level, __VA_ARGS__)
#define WMR_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->log_level, __VA_ARGS__)
#define WMR_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->log_level, __VA_ARGS__)
#define WMR_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->log_level, __VA_ARGS__)
#define WMR_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->log_level, __VA_ARGS__)

#define SET_INPUT(NAME) (d->base.inputs[WMR_INDEX_##NAME].name = XRT_INPUT_WMR_##NAME)


static inline struct wmr_bt_controller *
wmr_bt_controller(struct xrt_device *p)
{
	return (struct wmr_bt_controller *)p;
}

static bool
read_packets(struct wmr_bt_controller *d)
{
	unsigned char buffer[WMR_MOTION_CONTROLLER_MSG_BUFFER_SIZE];

	// Better cpu efficiency with blocking reads instead of multiple reads.
	int size = os_hid_read(d->controller_hid, buffer, sizeof(buffer), 500);

	// Get the timing as close to reading packet as possible.
	uint64_t now_ns = os_monotonic_get_ns();

	if (size < 0) {
		WMR_ERROR(d, "WMR Controller (Bluetooth): Error reading from device");
		return false;
	} else if (size == 0) {
		WMR_TRACE(d, "WMR Controller (Bluetooth): No data to read from device");
		return true; // No more messages, return.
	}

	WMR_TRACE(d, "WMR Controller (Bluetooth): Read %u bytes from device", size);

	switch (buffer[0]) {
	case WMR_BT_MOTION_CONTROLLER_MSG:
		os_mutex_lock(&d->lock);
		// Note: skipping msg type byte
		bool b = wmr_controller_packet_parse(&buffer[1], (size_t)size - 1, &d->input, d->log_level);
		if (b) {
			m_imu_3dof_update(&d->fusion, d->input.imu.timestamp_ticks * WMR_MOTION_CONTROLLER_NS_PER_TICK,
			                  &d->input.imu.acc, &d->input.imu.gyro);

			d->last_imu_timestamp_ns = now_ns;
			d->last_angular_velocity = d->input.imu.gyro;

		} else {
			WMR_ERROR(d, "WMR Controller (Bluetooth): Failed parsing message type: %02x, size: %i",
			          buffer[0], size);
			os_mutex_unlock(&d->lock);
			return false;
		}
		os_mutex_unlock(&d->lock);
		break;
	default:
		WMR_DEBUG(d, "WMR Controller (Bluetooth): Unknown message type: %02x, size: %i", buffer[0], size);
		break;
	}

	return true;
}

static void
wmr_bt_controller_set_output(struct xrt_device *xdev, enum xrt_output_name name, union xrt_output_value *value)
{
	// struct wmr_bt_controller *d = wmr_bt_controller(xdev);
	// Todo: implement
}


static void
wmr_bt_controller_get_tracked_pose(struct xrt_device *xdev,
                                   enum xrt_input_name name,
                                   uint64_t at_timestamp_ns,
                                   struct xrt_space_relation *out_relation)
{
	struct wmr_bt_controller *d = wmr_bt_controller(xdev);

	// Variables needed for prediction.
	uint64_t last_imu_timestamp_ns = 0;
	struct xrt_space_relation relation = {0};
	relation.relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT | XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT);


	struct xrt_pose pose = {{0, 0, 0, 1}, {0, 1.2, -0.5}};
	if (xdev->device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) {
		pose.position.x = -0.2;
	} else {
		pose.position.x = 0.2;
	}
	relation.pose = pose;

	// Copy data while holding the lock.
	os_mutex_lock(&d->lock);
	relation.pose.orientation = d->fusion.rot;
	relation.angular_velocity = d->last_angular_velocity;
	last_imu_timestamp_ns = d->last_imu_timestamp_ns;
	os_mutex_unlock(&d->lock);

	// No prediction needed.
	if (at_timestamp_ns < last_imu_timestamp_ns) {
		*out_relation = relation;
		return;
	}

	uint64_t prediction_ns = at_timestamp_ns - last_imu_timestamp_ns;
	double prediction_s = time_ns_to_s(prediction_ns);

	m_predict_relation(&relation, prediction_s, out_relation);
}



static void
wmr_bt_controller_update_inputs(struct xrt_device *xdev)
{
	struct wmr_bt_controller *d = wmr_bt_controller(xdev);

	struct xrt_input *inputs = d->base.inputs;

	os_mutex_lock(&d->lock);

	inputs[WMR_INDEX_MENU_CLICK].value.boolean = d->input.menu;
	inputs[WMR_INDEX_SQUEEZE_CLICK].value.boolean = d->input.squeeze;
	inputs[WMR_INDEX_TRIGGER_VALUE].value.vec1.x = d->input.trigger;
	inputs[WMR_INDEX_THUMBSTICK_CLICK].value.boolean = d->input.thumbstick.click;
	inputs[WMR_INDEX_THUMBSTICK].value.vec2 = d->input.thumbstick.values;
	inputs[WMR_INDEX_TRACKPAD_CLICK].value.boolean = d->input.trackpad.click;
	inputs[WMR_INDEX_TRACKPAD_TOUCH].value.boolean = d->input.trackpad.touch;
	inputs[WMR_INDEX_TRACKPAD].value.vec2 = d->input.trackpad.values;

	os_mutex_unlock(&d->lock);
}

static void *
wmr_bt_controller_run_thread(void *ptr)
{
	struct wmr_bt_controller *d = wmr_bt_controller(ptr);

	os_thread_helper_lock(&d->controller_thread);
	while (os_thread_helper_is_running_locked(&d->controller_thread)) {
		os_thread_helper_unlock(&d->controller_thread);

		// Does not block.
		if (!read_packets(d)) {
			break;
		}
	}

	WMR_DEBUG(d, "WMR Controller (Bluetooth): Exiting reading thread.");

	return NULL;
}


static void
wmr_bt_controller_destroy(struct xrt_device *xdev)
{
	struct wmr_bt_controller *d = wmr_bt_controller(xdev);

	// Remove the variable tracking.
	u_var_remove_root(d);

	// Destroy the thread object.
	os_thread_helper_destroy(&d->controller_thread);

	if (d->controller_hid != NULL) {
		os_hid_destroy(d->controller_hid);
		d->controller_hid = NULL;
	}

	os_mutex_destroy(&d->lock);

	// Destroy the fusion.
	m_imu_3dof_close(&d->fusion);

	free(d);
}


/*
 *
 * Bindings
 *
 */

static struct xrt_binding_input_pair simple_inputs[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_WMR_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_WMR_MENU_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_WMR_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_WMR_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_WMR_HAPTIC},
};

static struct xrt_binding_profile binding_profiles[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs,
        .input_count = ARRAY_SIZE(simple_inputs),
        .outputs = simple_outputs,
        .output_count = ARRAY_SIZE(simple_outputs),
    },
};


/*
 *
 * 'Exported' functions.
 *
 */

struct xrt_device *
wmr_bt_controller_create(struct os_hid_device *controller_hid,
                         enum xrt_device_type controller_type,
                         enum u_logging_level log_level)
{

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	struct wmr_bt_controller *d = U_DEVICE_ALLOCATE(struct wmr_bt_controller, flags, 10, 1);

	d->log_level = log_level;
	d->controller_hid = controller_hid;

	if (controller_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) {
		snprintf(d->base.str, ARRAY_SIZE(d->base.str), "WMR Left Controller");
	} else {
		snprintf(d->base.str, ARRAY_SIZE(d->base.str), "WMR Right Controller");
	}

	d->base.destroy = wmr_bt_controller_destroy;
	d->base.get_tracked_pose = wmr_bt_controller_get_tracked_pose;
	d->base.set_output = wmr_bt_controller_set_output;
	d->base.update_inputs = wmr_bt_controller_update_inputs;

	SET_INPUT(MENU_CLICK);
	SET_INPUT(SQUEEZE_CLICK);
	SET_INPUT(TRIGGER_VALUE);
	SET_INPUT(THUMBSTICK_CLICK);
	SET_INPUT(THUMBSTICK);
	SET_INPUT(TRACKPAD_CLICK);
	SET_INPUT(TRACKPAD_TOUCH);
	SET_INPUT(TRACKPAD);
	SET_INPUT(GRIP_POSE);
	SET_INPUT(AIM_POSE);

	for (uint32_t i = 0; i < d->base.input_count; i++) {
		d->base.inputs[0].active = true;
	}

	d->base.outputs[0].name = XRT_OUTPUT_NAME_WMR_HAPTIC;

	d->base.binding_profiles = binding_profiles;
	d->base.binding_profile_count = ARRAY_SIZE(binding_profiles);

	d->base.name = XRT_DEVICE_WMR_CONTROLLER;
	d->base.device_type = controller_type;
	d->base.orientation_tracking_supported = true;
	d->base.position_tracking_supported = false;
	d->base.hand_tracking_supported = true;


	d->input.imu.timestamp_ticks = 0;
	m_imu_3dof_init(&d->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);



	int ret = 0;

	// Todo: Read config file from controller if possible.

	ret = os_mutex_init(&d->lock);
	if (ret != 0) {
		WMR_ERROR(d, "WMR Controller (Bluetooth): Failed to init mutex!");
		wmr_bt_controller_destroy(&d->base);
		return NULL;
	}

	// Thread and other state.
	ret = os_thread_helper_init(&d->controller_thread);
	if (ret != 0) {
		WMR_ERROR(d, "WMR Controller (Bluetooth): Failed to init controller threading!");
		wmr_bt_controller_destroy(&d->base);
		d = NULL;
		return NULL;
	}

	// Hand over controller device to reading thread.
	ret = os_thread_helper_start(&d->controller_thread, wmr_bt_controller_run_thread, d);
	if (ret != 0) {
		WMR_ERROR(d, "WMR Controller (Bluetooth): Failed to start controller thread!");
		wmr_bt_controller_destroy(&d->base);
		d = NULL;
		return NULL;
	}


	u_var_add_root(d, d->base.str, true);
	u_var_add_bool(d, &d->input.menu, "input.menu");
	u_var_add_bool(d, &d->input.home, "input.home");
	u_var_add_bool(d, &d->input.bt_pairing, "input.bt_pairing");
	u_var_add_bool(d, &d->input.squeeze, "input.squeeze");
	u_var_add_f32(d, &d->input.trigger, "input.trigger");
	u_var_add_u8(d, &d->input.battery, "input.battery");
	u_var_add_bool(d, &d->input.thumbstick.click, "input.thumbstick.click");
	u_var_add_f32(d, &d->input.thumbstick.values.x, "input.thumbstick.values.y");
	u_var_add_f32(d, &d->input.thumbstick.values.y, "input.thumbstick.values.x");
	u_var_add_bool(d, &d->input.trackpad.click, "input.trackpad.click");
	u_var_add_bool(d, &d->input.trackpad.touch, "input.trackpad.touch");
	u_var_add_f32(d, &d->input.trackpad.values.x, "input.trackpad.values.x");
	u_var_add_f32(d, &d->input.trackpad.values.y, "input.trackpad.values.y");
	u_var_add_ro_vec3_f32(d, &d->input.imu.acc, "imu.acc");
	u_var_add_ro_vec3_f32(d, &d->input.imu.gyro, "imu.gyro");
	u_var_add_i32(d, &d->input.imu.temperature, "imu.temperature");


	return &d->base;
}
