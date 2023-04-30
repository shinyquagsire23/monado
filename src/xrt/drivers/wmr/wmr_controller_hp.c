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

#include "wmr_controller.h"

#define WMR_TRACE(ctrl, ...) U_LOG_XDEV_IFL_T(&ctrl->base.base, ctrl->base.log_level, __VA_ARGS__)
#define WMR_TRACE_HEX(ctrl, ...) U_LOG_XDEV_IFL_T_HEX(&ctrl->base.base, ctrl->base.log_level, __VA_ARGS__)
#define WMR_DEBUG(ctrl, ...) U_LOG_XDEV_IFL_D(&ctrl->base.base, ctrl->base.log_level, __VA_ARGS__)
#define WMR_DEBUG_HEX(ctrl, ...) U_LOG_XDEV_IFL_D_HEX(&ctrl->base.base, ctrl->base.log_level, __VA_ARGS__)
#define WMR_INFO(ctrl, ...) U_LOG_XDEV_IFL_I(&ctrl->base.base, ctrl->base.log_level, __VA_ARGS__)
#define WMR_WARN(ctrl, ...) U_LOG_XDEV_IFL_W(&ctrl->base.base, ctrl->base.log_level, __VA_ARGS__)
#define WMR_ERROR(ctrl, ...) U_LOG_XDEV_IFL_E(&ctrl->base.base, ctrl->base.log_level, __VA_ARGS__)

#ifdef XRT_DOXYGEN
#define WMR_PACKED
#else
#define WMR_PACKED __attribute__((packed))
#endif

/*!
 * Indices in input list of each input.
 */
enum wmr_controller_hp_input_index
{
	WMR_CONTROLLER_INDEX_MENU_CLICK,
	WMR_CONTROLLER_INDEX_HOME_CLICK,
	WMR_CONTROLLER_INDEX_SQUEEZE_CLICK,
	WMR_CONTROLLER_INDEX_SQUEEZE_VALUE,
	WMR_CONTROLLER_INDEX_TRIGGER_VALUE,
	WMR_CONTROLLER_INDEX_THUMBSTICK_CLICK,
	WMR_CONTROLLER_INDEX_THUMBSTICK,
	WMR_CONTROLLER_INDEX_GRIP_POSE,
	WMR_CONTROLLER_INDEX_AIM_POSE,
	WMR_CONTROLLER_INDEX_X_A_CLICK,
	WMR_CONTROLLER_INDEX_Y_B_CLICK,
};

#define SET_INPUT(wcb, INDEX, NAME)                                                                                    \
	(wcb->base.inputs[WMR_CONTROLLER_INDEX_##INDEX].name = XRT_INPUT_G2_CONTROLLER_##NAME)

/*
 *
 * Bindings
 *
 */

static struct xrt_binding_input_pair simple_inputs[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_G2_CONTROLLER_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_G2_CONTROLLER_MENU_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_G2_CONTROLLER_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_G2_CONTROLLER_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_G2_CONTROLLER_HAPTIC},
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

/* OG WMR Controller inputs struct */
struct wmr_controller_hp_input
{
	// buttons clicked
	bool menu;
	bool home;
	bool bt_pairing;
	bool squeeze_click; // Squeeze click reported on full squeeze

	// X/Y/A/B buttons
	bool x_a;
	bool y_b;

	float trigger;
	float squeeze;

	struct
	{
		bool click;
		struct xrt_vec2 values;
	} thumbstick;

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

/* HP WMR Controller device struct */
struct wmr_controller_hp
{
	struct wmr_controller_base base;

	//! The last decoded package of IMU and button data
	struct wmr_controller_hp_input last_inputs;
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
	// @todo: Confirm the scale is correct

	out_vec->x = (float)sample[0] / (98000 / 2);
	out_vec->y = (float)sample[1] / (98000 / 2);
	out_vec->z = (float)sample[2] / (98000 / 2);
}


static inline void
vec3_from_wmr_controller_gyro(const int32_t sample[3], struct xrt_vec3 *out_vec)
{
	// @todo: Confirm the scale is correct
	out_vec->x = (float)sample[0] * 0.00001f;
	out_vec->y = (float)sample[1] * 0.00001f;
	out_vec->z = (float)sample[2] * 0.00001f;
}

static bool
wmr_controller_hp_packet_parse(struct wmr_controller_hp *ctrl, const unsigned char *buffer, size_t len)
{
	struct wmr_controller_hp_input *last_input = &ctrl->last_inputs;
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
	last_input->squeeze_click = buttons & 0x08; // squeeze-click
	last_input->bt_pairing = buttons & 0x20;

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

	/* On OG these are touchpad values, but on HP it's
	 * squeeze value and A_X/B_Y click */
	last_input->squeeze = (float)read8(&p) / 0xFF;

	buttons = read8(&p);
	last_input->x_a = buttons & 0x02;
	last_input->y_b = buttons & 0x01;

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
	struct wmr_controller_hp *ctrl = (struct wmr_controller_hp *)(wcb);

	bool b = wmr_controller_hp_packet_parse(ctrl, buffer, buf_size);
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
wmr_controller_hp_update_xrt_inputs(struct xrt_device *xdev)
{
	DRV_TRACE_MARKER();

	struct wmr_controller_hp *ctrl = (struct wmr_controller_hp *)(xdev);
	struct wmr_controller_base *wcb = (struct wmr_controller_base *)(xdev);

	os_mutex_lock(&wcb->data_lock);

	struct xrt_input *xrt_inputs = xdev->inputs;
	struct wmr_controller_hp_input *cur_inputs = &ctrl->last_inputs;

	xrt_inputs[WMR_CONTROLLER_INDEX_MENU_CLICK].value.boolean = cur_inputs->menu;
	xrt_inputs[WMR_CONTROLLER_INDEX_HOME_CLICK].value.boolean = cur_inputs->home;
	xrt_inputs[WMR_CONTROLLER_INDEX_X_A_CLICK].value.boolean = cur_inputs->x_a;
	xrt_inputs[WMR_CONTROLLER_INDEX_Y_B_CLICK].value.boolean = cur_inputs->y_b;
	xrt_inputs[WMR_CONTROLLER_INDEX_SQUEEZE_CLICK].value.boolean = cur_inputs->squeeze;
	xrt_inputs[WMR_CONTROLLER_INDEX_TRIGGER_VALUE].value.vec1.x = cur_inputs->trigger;
	xrt_inputs[WMR_CONTROLLER_INDEX_THUMBSTICK_CLICK].value.boolean = cur_inputs->thumbstick.click;
	xrt_inputs[WMR_CONTROLLER_INDEX_THUMBSTICK].value.vec2 = cur_inputs->thumbstick.values;

	os_mutex_unlock(&wcb->data_lock);
}

static void
wmr_controller_hp_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	DRV_TRACE_MARKER();

	// struct wmr_controller_base *d = wmr_controller_base(xdev);
	// Todo: implement
}

static void
wmr_controller_hp_destroy(struct xrt_device *xdev)
{
	struct wmr_controller_base *wcb = (struct wmr_controller_base *)(xdev);

	wmr_controller_base_deinit(wcb);
	free(wcb);
}

struct wmr_controller_base *
wmr_controller_hp_create(struct wmr_controller_connection *conn,
                         enum xrt_device_type controller_type,
                         enum u_logging_level log_level)
{
	DRV_TRACE_MARKER();

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	struct wmr_controller_hp *ctrl = U_DEVICE_ALLOCATE(struct wmr_controller_hp, flags, 11, 1);
	struct wmr_controller_base *wcb = (struct wmr_controller_base *)(ctrl);

	if (!wmr_controller_base_init(wcb, conn, controller_type, log_level)) {
		wmr_controller_hp_destroy(&wcb->base);
		return NULL;
	}

	wcb->handle_input_packet = handle_input_packet;

	wcb->base.name = XRT_DEVICE_HP_REVERB_G2_CONTROLLER;
	wcb->base.destroy = wmr_controller_hp_destroy;
	wcb->base.update_inputs = wmr_controller_hp_update_xrt_inputs;
	wcb->base.set_output = wmr_controller_hp_set_output;

	SET_INPUT(wcb, MENU_CLICK, MENU_CLICK);
	SET_INPUT(wcb, HOME_CLICK, HOME_CLICK);
	SET_INPUT(wcb, SQUEEZE_CLICK, SQUEEZE_CLICK);
	SET_INPUT(wcb, TRIGGER_VALUE, TRIGGER_VALUE);
	SET_INPUT(wcb, THUMBSTICK_CLICK, THUMBSTICK_CLICK);
	SET_INPUT(wcb, THUMBSTICK, THUMBSTICK);
	SET_INPUT(wcb, GRIP_POSE, GRIP_POSE);
	SET_INPUT(wcb, AIM_POSE, AIM_POSE);
	if (controller_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) {
		SET_INPUT(wcb, X_A_CLICK, X_CLICK);
		SET_INPUT(wcb, Y_B_CLICK, Y_CLICK);
	} else {
		SET_INPUT(wcb, X_A_CLICK, A_CLICK);
		SET_INPUT(wcb, Y_B_CLICK, B_CLICK);
	}

	for (uint32_t i = 0; i < wcb->base.input_count; i++) {
		wcb->base.inputs[0].active = true;
	}

	ctrl->last_inputs.imu.timestamp_ticks = 0;

	wcb->base.outputs[0].name = XRT_OUTPUT_NAME_WMR_HAPTIC;

	wcb->base.binding_profiles = binding_profiles;
	wcb->base.binding_profile_count = ARRAY_SIZE(binding_profiles);

	u_var_add_bool(wcb, &ctrl->last_inputs.menu, "input.menu");
	u_var_add_bool(wcb, &ctrl->last_inputs.home, "input.home");
	u_var_add_bool(wcb, &ctrl->last_inputs.bt_pairing, "input.bt_pairing");
	u_var_add_bool(wcb, &ctrl->last_inputs.squeeze_click, "input.squeeze.click");
	u_var_add_f32(wcb, &ctrl->last_inputs.squeeze, "input.squeeze.value");
	u_var_add_f32(wcb, &ctrl->last_inputs.trigger, "input.trigger");
	u_var_add_u8(wcb, &ctrl->last_inputs.battery, "input.battery");
	u_var_add_bool(wcb, &ctrl->last_inputs.thumbstick.click, "input.thumbstick.click");
	u_var_add_f32(wcb, &ctrl->last_inputs.thumbstick.values.x, "input.thumbstick.values.y");
	u_var_add_f32(wcb, &ctrl->last_inputs.thumbstick.values.y, "input.thumbstick.values.x");
	if (controller_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) {
		u_var_add_bool(wcb, &ctrl->last_inputs.x_a, "input.x");
		u_var_add_bool(wcb, &ctrl->last_inputs.y_b, "input.y");
	} else {
		u_var_add_bool(wcb, &ctrl->last_inputs.x_a, "input.a");
		u_var_add_bool(wcb, &ctrl->last_inputs.y_b, "input.b");
	}

	u_var_add_ro_vec3_f32(wcb, &ctrl->last_inputs.imu.acc, "imu.acc");
	u_var_add_ro_vec3_f32(wcb, &ctrl->last_inputs.imu.gyro, "imu.gyro");
	u_var_add_i32(wcb, &ctrl->last_inputs.imu.temperature, "imu.temperature");

	return wcb;
}
