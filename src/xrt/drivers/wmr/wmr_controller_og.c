// Copyright 2020-2021, N Madsen.
// Copyright 2020-2023, Collabora, Ltd.
// Copyright 2020-2023, Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Driver for WMR Controllers.
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_wmr
 */
#include "util/u_device.h"
#include "util/u_trace_marker.h"
#include "util/u_var.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "wmr_common.h"
#include "wmr_controller.h"

#ifdef XRT_DOXYGEN
#define WMR_PACKED
#else
#define WMR_PACKED __attribute__((packed))
#endif

/*!
 * Indices in input list of each input.
 */
enum wmr_controller_og_input_index
{
	WMR_CONTROLLER_INDEX_MENU_CLICK,
	WMR_CONTROLLER_INDEX_HOME_CLICK,
	WMR_CONTROLLER_INDEX_SQUEEZE_CLICK,
	WMR_CONTROLLER_INDEX_TRIGGER_VALUE,
	WMR_CONTROLLER_INDEX_THUMBSTICK_CLICK,
	WMR_CONTROLLER_INDEX_THUMBSTICK,
	WMR_CONTROLLER_INDEX_TRACKPAD_CLICK,
	WMR_CONTROLLER_INDEX_TRACKPAD_TOUCH,
	WMR_CONTROLLER_INDEX_TRACKPAD,
	WMR_CONTROLLER_INDEX_GRIP_POSE,
	WMR_CONTROLLER_INDEX_AIM_POSE,
};

#define SET_WMR_INPUT(wcb, NAME) (wcb->base.inputs[WMR_CONTROLLER_INDEX_##NAME].name = XRT_INPUT_WMR_##NAME)
#define SET_ODYSSEY_INPUT(wcb, NAME)                                                                                   \
	(wcb->base.inputs[WMR_CONTROLLER_INDEX_##NAME].name = XRT_INPUT_ODYSSEY_CONTROLLER_##NAME)

/*
 *
 * Bindings
 *
 */

static struct xrt_binding_input_pair simple_inputs_og[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_WMR_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_WMR_MENU_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_WMR_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_WMR_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs_og[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_WMR_HAPTIC},
};

static struct xrt_binding_profile binding_profiles_og[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs_og,
        .input_count = ARRAY_SIZE(simple_inputs_og),
        .outputs = simple_outputs_og,
        .output_count = ARRAY_SIZE(simple_outputs_og),
    },
};

static struct xrt_binding_input_pair simple_inputs_odyssey[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_ODYSSEY_CONTROLLER_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_ODYSSEY_CONTROLLER_MENU_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_ODYSSEY_CONTROLLER_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_ODYSSEY_CONTROLLER_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs_odyssey[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_ODYSSEY_CONTROLLER_HAPTIC},
};

static struct xrt_binding_profile binding_profiles_odyssey[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs_odyssey,
        .input_count = ARRAY_SIZE(simple_inputs_odyssey),
        .outputs = simple_outputs_odyssey,
        .output_count = ARRAY_SIZE(simple_outputs_odyssey),
    },
};


/* OG WMR Controller inputs struct */
struct wmr_controller_og_input
{
	// buttons clicked
	bool menu;
	bool home;
	bool bt_pairing;
	bool squeeze; // Actually a "squeeze" click

	float trigger;

	struct
	{
		bool click;
		struct xrt_vec2 values;
	} thumbstick;
	struct
	{
		bool click;
		bool touch;
		struct xrt_vec2 values;
	} trackpad;

	uint8_t battery;

	struct
	{
		uint64_t timestamp_ticks;
		struct xrt_vec3 acc;
		struct xrt_vec3 gyro;
		int32_t temperature;
	} imu;
};
#undef WMR_PACKED

/* OG WMR Controller device struct */
struct wmr_controller_og
{
	struct wmr_controller_base base;

	//! The last decoded package of IMU and button data
	struct wmr_controller_og_input last_inputs;
};

/*
 *
 * WMR Motion Controller protocol helpers
 *
 */

static inline void
vec3_from_wmr_controller_accel(const int32_t sample[3], struct xrt_vec3 *out_vec)
{
	// Reverb G1 observation: 1g is approximately 490,000.

	out_vec->x = (float)sample[0] / (98000 / 2);
	out_vec->y = (float)sample[1] / (98000 / 2);
	out_vec->z = (float)sample[2] / (98000 / 2);
}


static inline void
vec3_from_wmr_controller_gyro(const int32_t sample[3], struct xrt_vec3 *out_vec)
{
	out_vec->x = (float)sample[0] * 0.00001f;
	out_vec->y = (float)sample[1] * 0.00001f;
	out_vec->z = (float)sample[2] * 0.00001f;
}

static bool
wmr_controller_og_packet_parse(struct wmr_controller_og *ctrl, const unsigned char *buffer, size_t len)
{
	struct wmr_controller_og_input *last_input = &ctrl->last_inputs;
	struct wmr_controller_base *wcb = (struct wmr_controller_base *)(ctrl);

	if (len != 44) {
		U_LOG_IFL_E(wcb->log_level, "WMR Controller: unexpected message length: %zd", len);
		return false;
	}

	const unsigned char *p = buffer;

	// Read buttons
	uint8_t buttons = read8(&p);
	last_input->thumbstick.click = buttons & 0x01;
	last_input->home = buttons & 0x02;
	last_input->menu = buttons & 0x04;
	last_input->squeeze = buttons & 0x08; // squeeze-click
	last_input->trackpad.click = buttons & 0x10;
	last_input->bt_pairing = buttons & 0x20;
	last_input->trackpad.touch = buttons & 0x40;


	// Read thumbstick coordinates (12 bit resolution)
	int16_t stick_x = read8(&p);
	uint8_t nibbles = read8(&p);
	stick_x += ((nibbles & 0x0F) << 8);
	int16_t stick_y = (nibbles >> 4);
	stick_y += (read8(&p) << 4);

	last_input->thumbstick.values.x = (float)(stick_x - 0x07FF) / 0x07FF;
	if (last_input->thumbstick.values.x > 1.0f) {
		last_input->thumbstick.values.x = 1.0f;
	}

	last_input->thumbstick.values.y = (float)(stick_y - 0x07FF) / 0x07FF;
	if (last_input->thumbstick.values.y > 1.0f) {
		last_input->thumbstick.values.y = 1.0f;
	}

	// Read trigger value (0x00 - 0xFF)
	last_input->trigger = (float)read8(&p) / 0xFF;

	// Read trackpad coordinates (0x00 - 0x64. Both are 0xFF when untouched)
	uint8_t trackpad_x = read8(&p);
	uint8_t trackpad_y = read8(&p);
	last_input->trackpad.values.x = (trackpad_x == 0xFF) ? 0.0f : (float)(trackpad_x - 0x32) / 0x32;
	last_input->trackpad.values.y = (trackpad_y == 0xFF) ? 0.0f : (float)(trackpad_y - 0x32) / 0x32;

	last_input->battery = read8(&p);

	int32_t acc[3];
	acc[0] = read24(&p); // x
	acc[1] = read24(&p); // y
	acc[2] = read24(&p); // z
	vec3_from_wmr_controller_accel(acc, &last_input->imu.acc);

	U_LOG_IFL_T(wcb->log_level, "Accel [m/s^2] : %f",
	            sqrtf(last_input->imu.acc.x * last_input->imu.acc.x +
	                  last_input->imu.acc.y * last_input->imu.acc.y +
	                  last_input->imu.acc.z * last_input->imu.acc.z));


	last_input->imu.temperature = read16(&p);

	int32_t gyro[3];
	gyro[0] = read24(&p);
	gyro[1] = read24(&p);
	gyro[2] = read24(&p);
	vec3_from_wmr_controller_gyro(gyro, &last_input->imu.gyro);


	uint32_t prev_ticks = last_input->imu.timestamp_ticks & UINT32_C(0xFFFFFFFF);

	// Write the new ticks value into the lower half of timestamp_ticks
	last_input->imu.timestamp_ticks &= (UINT64_C(0xFFFFFFFF) << 32u);
	last_input->imu.timestamp_ticks += (uint32_t)read32(&p);

	if ((last_input->imu.timestamp_ticks & UINT64_C(0xFFFFFFFF)) < prev_ticks) {
		// Timer overflow, so increment the upper half of timestamp_ticks
		last_input->imu.timestamp_ticks += (UINT64_C(0x1) << 32u);
	}

	/* Todo: More decoding here
	    read16(&p); // Unknown. Seems to depend on controller orientation (probably mag)
	    read32(&p); // Unknown.
	    read16(&p); // Unknown. Device state, etc.
	    read16(&p);
	    read16(&p);
	*/

	return true;
}

static bool
handle_input_packet(struct wmr_controller_base *wcb, uint64_t time_ns, uint8_t *buffer, uint32_t buf_size)
{
	struct wmr_controller_og *ctrl = (struct wmr_controller_og *)(wcb);
	bool b = wmr_controller_og_packet_parse(ctrl, buffer, buf_size);
	if (b) {
		m_imu_3dof_update(&wcb->fusion,
		                  ctrl->last_inputs.imu.timestamp_ticks * WMR_MOTION_CONTROLLER_NS_PER_TICK,
		                  &ctrl->last_inputs.imu.acc, &ctrl->last_inputs.imu.gyro);

		wcb->last_imu_timestamp_ns = time_ns;
		wcb->last_angular_velocity = ctrl->last_inputs.imu.gyro;
	}

	return b;
}

static void
wmr_controller_og_update_xrt_inputs(struct xrt_device *xdev)
{
	DRV_TRACE_MARKER();

	struct wmr_controller_og *ctrl = (struct wmr_controller_og *)(xdev);
	struct wmr_controller_base *wcb = (struct wmr_controller_base *)(xdev);

	os_mutex_lock(&wcb->data_lock);

	struct xrt_input *xrt_inputs = xdev->inputs;
	struct wmr_controller_og_input *cur_inputs = &ctrl->last_inputs;

	xrt_inputs[WMR_CONTROLLER_INDEX_MENU_CLICK].value.boolean = cur_inputs->menu;
	xrt_inputs[WMR_CONTROLLER_INDEX_HOME_CLICK].value.boolean = cur_inputs->home;
	xrt_inputs[WMR_CONTROLLER_INDEX_SQUEEZE_CLICK].value.boolean = cur_inputs->squeeze;
	xrt_inputs[WMR_CONTROLLER_INDEX_TRIGGER_VALUE].value.vec1.x = cur_inputs->trigger;
	xrt_inputs[WMR_CONTROLLER_INDEX_THUMBSTICK_CLICK].value.boolean = cur_inputs->thumbstick.click;
	xrt_inputs[WMR_CONTROLLER_INDEX_THUMBSTICK].value.vec2 = cur_inputs->thumbstick.values;
	xrt_inputs[WMR_CONTROLLER_INDEX_TRACKPAD_CLICK].value.boolean = cur_inputs->trackpad.click;
	xrt_inputs[WMR_CONTROLLER_INDEX_TRACKPAD_TOUCH].value.boolean = cur_inputs->trackpad.touch;
	xrt_inputs[WMR_CONTROLLER_INDEX_TRACKPAD].value.vec2 = cur_inputs->trackpad.values;

	os_mutex_unlock(&wcb->data_lock);
}

static void
wmr_controller_og_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	DRV_TRACE_MARKER();

	// struct wmr_controller_base *d = wmr_controller_base(xdev);
	// Todo: implement
}

static void
wmr_controller_og_destroy(struct xrt_device *xdev)
{
	struct wmr_controller_base *wcb = (struct wmr_controller_base *)(xdev);

	wmr_controller_base_deinit(wcb);
	free(wcb);
}

struct wmr_controller_base *
wmr_controller_og_create(struct wmr_controller_connection *conn,
                         enum xrt_device_type controller_type,
                         uint16_t pid,
                         enum u_logging_level log_level)
{
	DRV_TRACE_MARKER();

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	struct wmr_controller_og *ctrl = U_DEVICE_ALLOCATE(struct wmr_controller_og, flags, 11, 1);
	struct wmr_controller_base *wcb = (struct wmr_controller_base *)(ctrl);

	if (!wmr_controller_base_init(wcb, conn, controller_type, log_level)) {
		wmr_controller_og_destroy(&wcb->base);
		return NULL;
	}

	wcb->handle_input_packet = handle_input_packet;

	if (pid == ODYSSEY_CONTROLLER_PID) {
		wcb->base.name = XRT_DEVICE_SAMSUNG_ODYSSEY_CONTROLLER;
	} else {
		wcb->base.name = XRT_DEVICE_WMR_CONTROLLER;
	}
	wcb->base.destroy = wmr_controller_og_destroy;
	wcb->base.update_inputs = wmr_controller_og_update_xrt_inputs;
	wcb->base.set_output = wmr_controller_og_set_output;

	if (pid == ODYSSEY_CONTROLLER_PID) {
		SET_ODYSSEY_INPUT(wcb, MENU_CLICK);
		SET_ODYSSEY_INPUT(wcb, HOME_CLICK);
		SET_ODYSSEY_INPUT(wcb, SQUEEZE_CLICK);
		SET_ODYSSEY_INPUT(wcb, TRIGGER_VALUE);
		SET_ODYSSEY_INPUT(wcb, THUMBSTICK_CLICK);
		SET_ODYSSEY_INPUT(wcb, THUMBSTICK);
		SET_ODYSSEY_INPUT(wcb, TRACKPAD_CLICK);
		SET_ODYSSEY_INPUT(wcb, TRACKPAD_TOUCH);
		SET_ODYSSEY_INPUT(wcb, TRACKPAD);
		SET_ODYSSEY_INPUT(wcb, GRIP_POSE);
		SET_ODYSSEY_INPUT(wcb, AIM_POSE);

		wcb->base.outputs[0].name = XRT_OUTPUT_NAME_ODYSSEY_CONTROLLER_HAPTIC;

		wcb->base.binding_profiles = binding_profiles_odyssey;
		wcb->base.binding_profile_count = ARRAY_SIZE(binding_profiles_odyssey);
	} else {
		SET_WMR_INPUT(wcb, MENU_CLICK);
		SET_WMR_INPUT(wcb, HOME_CLICK);
		SET_WMR_INPUT(wcb, SQUEEZE_CLICK);
		SET_WMR_INPUT(wcb, TRIGGER_VALUE);
		SET_WMR_INPUT(wcb, THUMBSTICK_CLICK);
		SET_WMR_INPUT(wcb, THUMBSTICK);
		SET_WMR_INPUT(wcb, TRACKPAD_CLICK);
		SET_WMR_INPUT(wcb, TRACKPAD_TOUCH);
		SET_WMR_INPUT(wcb, TRACKPAD);
		SET_WMR_INPUT(wcb, GRIP_POSE);
		SET_WMR_INPUT(wcb, AIM_POSE);

		wcb->base.outputs[0].name = XRT_OUTPUT_NAME_WMR_HAPTIC;

		wcb->base.binding_profiles = binding_profiles_og;
		wcb->base.binding_profile_count = ARRAY_SIZE(binding_profiles_og);
	}

	for (uint32_t i = 0; i < wcb->base.input_count; i++) {
		wcb->base.inputs[0].active = true;
	}

	ctrl->last_inputs.imu.timestamp_ticks = 0;

	u_var_add_bool(wcb, &ctrl->last_inputs.menu, "input.menu");
	u_var_add_bool(wcb, &ctrl->last_inputs.home, "input.home");
	u_var_add_bool(wcb, &ctrl->last_inputs.bt_pairing, "input.bt_pairing");
	u_var_add_bool(wcb, &ctrl->last_inputs.squeeze, "input.squeeze");
	u_var_add_f32(wcb, &ctrl->last_inputs.trigger, "input.trigger");
	u_var_add_u8(wcb, &ctrl->last_inputs.battery, "input.battery");
	u_var_add_bool(wcb, &ctrl->last_inputs.thumbstick.click, "input.thumbstick.click");
	u_var_add_f32(wcb, &ctrl->last_inputs.thumbstick.values.x, "input.thumbstick.values.y");
	u_var_add_f32(wcb, &ctrl->last_inputs.thumbstick.values.y, "input.thumbstick.values.x");
	u_var_add_bool(wcb, &ctrl->last_inputs.trackpad.click, "input.trackpad.click");
	u_var_add_bool(wcb, &ctrl->last_inputs.trackpad.touch, "input.trackpad.touch");
	u_var_add_f32(wcb, &ctrl->last_inputs.trackpad.values.x, "input.trackpad.values.x");
	u_var_add_f32(wcb, &ctrl->last_inputs.trackpad.values.y, "input.trackpad.values.y");
	u_var_add_ro_vec3_f32(wcb, &ctrl->last_inputs.imu.acc, "imu.acc");
	u_var_add_ro_vec3_f32(wcb, &ctrl->last_inputs.imu.gyro, "imu.gyro");
	u_var_add_i32(wcb, &ctrl->last_inputs.imu.temperature, "imu.temperature");

	return wcb;
}
