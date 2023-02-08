/*
 * Copyright 2020 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 * OpenHMD - Free and Open Source API and drivers for immersive technology.
 */
/*!
 * @file
 * @brief  Oculus Rift S Touch Controller driver
 *
 * Handles communication and calibration information for the Touch Controllers
 *
 * Ported from OpenHMD
 *
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */


#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

#include "math/m_api.h"
#include "math/m_space.h"
#include "math/m_vec3.h"

#include "os/os_hid.h"

#include "util/u_device.h"
#include "util/u_trace_marker.h"
#include "util/u_var.h"

#include "rift_s.h"
#include "rift_s_hmd.h"
#include "rift_s_radio.h"
#include "rift_s_protocol.h"
#include "rift_s_controller.h"

/* Set to 1 to print controller states continuously */
#define DUMP_CONTROLLER_STATE 0

#define DEG_TO_RAD(D) ((D)*M_PI / 180.)

static struct xrt_binding_input_pair simple_inputs_rift_s[4] = {
    {XRT_INPUT_SIMPLE_SELECT_CLICK, XRT_INPUT_TOUCH_TRIGGER_VALUE},
    {XRT_INPUT_SIMPLE_MENU_CLICK, XRT_INPUT_TOUCH_MENU_CLICK},
    {XRT_INPUT_SIMPLE_GRIP_POSE, XRT_INPUT_TOUCH_GRIP_POSE},
    {XRT_INPUT_SIMPLE_AIM_POSE, XRT_INPUT_TOUCH_AIM_POSE},
};

static struct xrt_binding_output_pair simple_outputs_rift_s[1] = {
    {XRT_OUTPUT_NAME_SIMPLE_VIBRATION, XRT_OUTPUT_NAME_TOUCH_HAPTIC},
};

static struct xrt_binding_profile binding_profiles_rift_s[1] = {
    {
        .name = XRT_DEVICE_SIMPLE_CONTROLLER,
        .inputs = simple_inputs_rift_s,
        .input_count = ARRAY_SIZE(simple_inputs_rift_s),
        .outputs = simple_outputs_rift_s,
        .output_count = ARRAY_SIZE(simple_outputs_rift_s),
    },
};

enum touch_controller_input_index
{
	/* Left controller */
	OCULUS_TOUCH_X_CLICK = 0,
	OCULUS_TOUCH_X_TOUCH,
	OCULUS_TOUCH_Y_CLICK,
	OCULUS_TOUCH_Y_TOUCH,
	OCULUS_TOUCH_MENU_CLICK,

	/* Right controller */
	OCULUS_TOUCH_A_CLICK = 0,
	OCULUS_TOUCH_A_TOUCH,
	OCULUS_TOUCH_B_CLICK,
	OCULUS_TOUCH_B_TOUCH,
	OCULUS_TOUCH_SYSTEM_CLICK,

	/* Common */
	OCULUS_TOUCH_SQUEEZE_VALUE,
	OCULUS_TOUCH_TRIGGER_TOUCH,
	OCULUS_TOUCH_TRIGGER_VALUE,
	OCULUS_TOUCH_THUMBSTICK_CLICK,
	OCULUS_TOUCH_THUMBSTICK_TOUCH,
	OCULUS_TOUCH_THUMBSTICK,
	OCULUS_TOUCH_THUMBREST_TOUCH,
	OCULUS_TOUCH_GRIP_POSE,
	OCULUS_TOUCH_AIM_POSE,

	INPUT_INDICES_LAST
};
#define SET_TOUCH_INPUT(d, NAME) ((d)->base.inputs[OCULUS_TOUCH_##NAME].name = XRT_INPUT_TOUCH_##NAME)
#define DEBUG_TOUCH_INPUT_BOOL(d, NAME, label)                                                                         \
	u_var_add_bool((d), &(d)->base.inputs[OCULUS_TOUCH_##NAME].value.boolean, label)
#define DEBUG_TOUCH_INPUT_F32(d, NAME, label)                                                                          \
	u_var_add_f32((d), &(d)->base.inputs[OCULUS_TOUCH_##NAME].value.vec1.x, label)
#define DEBUG_TOUCH_INPUT_VEC2(d, NAME, label1, label2)                                                                \
	u_var_add_f32((d), &(d)->base.inputs[OCULUS_TOUCH_##NAME].value.vec2.x, label1);                               \
	u_var_add_f32((d), &(d)->base.inputs[OCULUS_TOUCH_##NAME].value.vec2.y, label2)

#if DUMP_CONTROLLER_STATE
static void
print_controller_state(struct rift_s_controller *ctrl)
{
	if (rift_s_log_level > U_LOGGING_TRACE)
		return; // Only log at TRACE log_level

	/* Dump the controller state if we see something unexpected / unknown, otherwise be quiet */
	if (ctrl->extra_bytes_len == 0 && ctrl->mask08 == 0x50 && ctrl->mask0e == 0)
		return;

	char buf[16384] = "";
	int bufsize = sizeof(buf) - 2;
	int printed = 0;

	printed += snprintf(buf + printed, bufsize - printed,
	                    "Controller %16lx type 0x%08x IMU ts %8u v2 %x accel %6d %6d %6d gyro %6d %6d %6d | ",
	                    ctrl->device_id, ctrl->device_type, ctrl->imu_timestamp32, ctrl->imu_unknown_varying2,
	                    ctrl->raw_accel[0], ctrl->raw_accel[1], ctrl->raw_accel[2], ctrl->raw_gyro[0],
	                    ctrl->raw_gyro[1], ctrl->raw_gyro[2]);

	printed += snprintf(buf + printed, bufsize - printed, "unk %02x %02x buttons %02x fingers %02x | ",
	                    ctrl->mask08, ctrl->mask0e, ctrl->buttons, ctrl->fingers);
	printed += snprintf(buf + printed, bufsize - printed, "trigger %5d grip %5d |", ctrl->trigger, ctrl->grip);
	printed +=
	    snprintf(buf + printed, bufsize - printed, "joystick x %5d y %5d |", ctrl->joystick_x, ctrl->joystick_y);

	if (ctrl->device_type == RIFT_S_DEVICE_LEFT_CONTROLLER) {
		printed +=
		    snprintf(buf + printed, bufsize - printed, "capsense x %u y %u joy %u trig %u | ",
		             ctrl->capsense_a_x, ctrl->capsense_b_y, ctrl->capsense_joystick, ctrl->capsense_trigger);
	} else if (ctrl->device_type == RIFT_S_DEVICE_RIGHT_CONTROLLER) {
		printed +=
		    snprintf(buf + printed, bufsize - printed, "capsense a %u b %u joy %u trig %u | ",
		             ctrl->capsense_a_x, ctrl->capsense_b_y, ctrl->capsense_joystick, ctrl->capsense_trigger);
	} else {
		printed +=
		    snprintf(buf + printed, bufsize - printed, "capsense ?? %u ?? %u ?? %u ?? %u | ",
		             ctrl->capsense_a_x, ctrl->capsense_b_y, ctrl->capsense_joystick, ctrl->capsense_trigger);
	}

	if (ctrl->extra_bytes_len) {
		printed += snprintf(buf + printed, bufsize - printed, " | extra ");
		printed += rift_s_snprintf_hexdump_buffer(buf + printed, bufsize - printed, NULL, ctrl->extra_bytes,
		                                          ctrl->extra_bytes_len);
	}

	RIFT_S_TRACE("%s", buf);
}
#endif

static void
handle_imu_update(struct rift_s_controller *ctrl,
                  timepoint_ns local_ts,
                  uint32_t imu_timestamp,
                  const int16_t raw_accel[3],
                  const int16_t raw_gyro[3])
{
	/* Logic to update 64-bit ns timestamp from
	 * 32-bit µS device timestamp that wraps every 71.5 minutes */
	uint32_t dt = 0;

	if (ctrl->imu_time_valid) {
		dt = imu_timestamp - ctrl->imu_timestamp32;

		/* Sometimes we see 1-2 repeated IMU updates from a controller,
		 * that must be ignored or else time jumps wildly */
		if (dt == 0 || dt > 2147483648) {
			RIFT_S_TRACE("Controller %" PRIx64 " - ignoring repeated IMU update", ctrl->device_id);
			return;
		}

		ctrl->last_imu_device_time_ns += (timepoint_ns)dt * OS_NS_PER_USEC;
	} else {
		ctrl->last_imu_device_time_ns = (timepoint_ns)imu_timestamp * OS_NS_PER_USEC;
		ctrl->imu_time_valid = true;
	}
	ctrl->imu_timestamp32 = imu_timestamp;
	ctrl->last_imu_local_time_ns = local_ts;

	if (!ctrl->have_calibration || !ctrl->have_config)
		return; /* We need to finish reading the calibration or config blocks first */

	const float gyro_scale = ctrl->config.gyro_scale;
	const float accel_scale = MATH_GRAVITY_M_S2 * ctrl->config.accel_scale;

	struct xrt_vec3 gyro, accel;

	gyro.x = DEG_TO_RAD(gyro_scale * raw_gyro[0]);
	gyro.y = DEG_TO_RAD(gyro_scale * raw_gyro[1]);
	gyro.z = DEG_TO_RAD(gyro_scale * raw_gyro[2]);

	accel.x = accel_scale * raw_accel[0];
	accel.y = accel_scale * raw_accel[1];
	accel.z = accel_scale * raw_accel[2];

	/* Apply correction offsets first, then rectify */
	accel = m_vec3_sub(accel, ctrl->calibration.accel.offset);
	gyro = m_vec3_sub(gyro, ctrl->calibration.gyro.offset);

	math_matrix_3x3_transform_vec3(&ctrl->calibration.accel.rectification, &accel, &ctrl->accel);
	math_matrix_3x3_transform_vec3(&ctrl->calibration.gyro.rectification, &gyro, &ctrl->gyro);

	m_imu_3dof_update(&ctrl->fusion, ctrl->last_imu_device_time_ns, &ctrl->accel, &ctrl->gyro);
	ctrl->pose.orientation = ctrl->fusion.rot;

#if 0
	RIFT_S_DEBUG("%" PRIx64 " dt %u device time %u ns %" PRIu64
	             " raw accel %d %d %d gyro %d %d %d -> accel %f %f %f  gyro %f %f %f\n",
	             ctrl->device_id, dt, imu_timestamp, ctrl->last_imu_device_time_ns, raw_accel[0], raw_accel[1],
	             raw_accel[2], raw_gyro[0], raw_gyro[1], raw_gyro[2], ctrl->accel.x, ctrl->accel.y, ctrl->accel.z,
	             ctrl->gyro.x, ctrl->gyro.y, ctrl->gyro.z);
#endif
}

bool
rift_s_controller_handle_report(struct rift_s_controller *ctrl,
                                timepoint_ns local_ts,
                                rift_s_controller_report_t *report)
{
#if DUMP_CONTROLLER_STATE
	bool saw_imu_update = false;
#endif
	bool saw_controls_update = false;

	os_mutex_lock(&ctrl->mutex);

	/* Collect state updates */
	ctrl->extra_bytes_len = 0;

	for (int i = 0; i < report->num_info; i++) {
		rift_s_controller_info_block_t *info = report->info + i;

		switch (info->block_id) {
		case RIFT_S_CTRL_MASK08:
			saw_controls_update = true;
			ctrl->mask08 = info->maskbyte.val;
			break;
		case RIFT_S_CTRL_BUTTONS:
			saw_controls_update = true;
			ctrl->buttons = info->maskbyte.val;
			break;
		case RIFT_S_CTRL_FINGERS:
			saw_controls_update = true;
			ctrl->fingers = info->maskbyte.val;
			break;
		case RIFT_S_CTRL_MASK0e:
			saw_controls_update = true;
			ctrl->mask0e = info->maskbyte.val;
			break;
		case RIFT_S_CTRL_TRIGGRIP: {
			saw_controls_update = true;
			ctrl->trigger = (uint16_t)(info->triggrip.vals[1] & 0x0f) << 8 | info->triggrip.vals[0];
			ctrl->grip =
			    (uint16_t)(info->triggrip.vals[1] & 0xf0) >> 4 | ((uint16_t)(info->triggrip.vals[2]) << 4);
			break;
		}
		case RIFT_S_CTRL_JOYSTICK:
			saw_controls_update = true;
			ctrl->joystick_x = info->joystick.val;
			ctrl->joystick_y = info->joystick.val >> 16;
			break;
		case RIFT_S_CTRL_CAPSENSE:
			saw_controls_update = true;
			ctrl->capsense_a_x = info->capsense.a_x;
			ctrl->capsense_b_y = info->capsense.b_y;
			ctrl->capsense_joystick = info->capsense.joystick;
			ctrl->capsense_trigger = info->capsense.trigger;
			break;
		case RIFT_S_CTRL_IMU: {
			int j;

#if DUMP_CONTROLLER_STATE
			/* print the state before updating the IMU timestamp a 2nd time */
			if (saw_imu_update)
				print_controller_state(ctrl);
			saw_imu_update = true;
#endif

			ctrl->imu_unknown_varying2 = info->imu.unknown_varying2;

			for (j = 0; j < 3; j++) {
				ctrl->raw_accel[j] = info->imu.accel[j];
				ctrl->raw_gyro[j] = info->imu.gyro[j];
			}
			handle_imu_update(ctrl, local_ts, info->imu.timestamp, ctrl->raw_accel, ctrl->raw_gyro);
			break;
		}
		default:
			RIFT_S_WARN("Invalid controller info block with ID %02x from device %08" PRIx64
			            ". Please report it.\n",
			            info->block_id, ctrl->device_id);
		}
	}

	if (saw_controls_update)
		ctrl->last_controls_local_time_ns = local_ts;

	if (report->extra_bytes_len > 0) {
		if (report->extra_bytes_len > sizeof(ctrl->extra_bytes)) {
			RIFT_S_WARN("Controller report from %16" PRIx64 " had too many extra bytes - %u (max %u)\n",
			            ctrl->device_id, report->extra_bytes_len,
			            (unsigned int)(sizeof(ctrl->extra_bytes)));
			report->extra_bytes_len = sizeof(ctrl->extra_bytes);
		}
		memcpy(ctrl->extra_bytes, report->extra_bytes, report->extra_bytes_len);
	}
	ctrl->extra_bytes_len = report->extra_bytes_len;

#if DUMP_CONTROLLER_STATE
	print_controller_state(ctrl);
#endif

	/* Finally, update and output the log */
	if (report->flags & 0x04) {
		/* New log line is starting, reset the counter */
		ctrl->log_bytes = 0;
	}

	if (ctrl->log_flags & 0x04 || (ctrl->log_flags & 0x02) != (report->flags & 0x02)) {
		/* New log bytes in this report, collect them */
		for (int i = 0; i < 3; i++) {
			uint8_t c = report->log[i];
			if (c != '\0') {
				if (ctrl->log_bytes == (MAX_LOG_SIZE - 1)) {
					/* Log line got too long... output it */
					ctrl->log[MAX_LOG_SIZE - 1] = '\0';
					RIFT_S_DEBUG("Controller: %s", ctrl->log);
					ctrl->log_bytes = 0;
				}
				ctrl->log[ctrl->log_bytes++] = c;
			} else if (ctrl->log_bytes > 0) {
				/* Found the end of the string */
				ctrl->log[ctrl->log_bytes] = '\0';
				rift_s_hexdump_buffer("Controller debug", ctrl->log, ctrl->log_bytes);
				ctrl->log_bytes = 0;
			}
		}
	}
	ctrl->log_flags = report->flags;

	os_mutex_unlock(&ctrl->mutex);
	return true;
}

#define READ_LE16(b) (b)[0] | ((b)[1]) << 8
#define READ_LE32(b) (b)[0] | ((b)[1]) << 8 | ((b)[2]) << 16 | ((b)[3]) << 24
#define READ_LEFLOAT32(b) (*(float *)(b));

static void
ctrl_config_cb(bool success, uint8_t *response_bytes, int response_bytes_len, struct rift_s_controller *ctrl)
{
	ctrl->reading_config = false;
	if (!success) {
		RIFT_S_WARN("Failed to read controller config");
		return;
	}

	if (response_bytes_len < 5) {
		RIFT_S_WARN("Failed to read controller config - short result");
		return;
	}

	/* Response 0u32 0x10   00 7d a0 0f f4 01 f4 01 00 00 80 3a ff ff f9 3d
	 *   0x7d00 = 32000 0x0fa0 = 4000 0x01f4 = 500 0x01f4 = 500
	 *   0x3a800000 = 0.9765625e-03  = 1/1024
	 *   0x3df9ffff = 0.1220703      = 1/8192
	 */

	response_bytes_len = response_bytes[4];
	if (response_bytes_len < 16) {
		char buf[16384] = "";
		int bufsize = sizeof(buf) - 2;
		int printed = 0;

		printed += rift_s_snprintf_hexdump_buffer(buf + printed, bufsize - printed, "Controller Config",
		                                          response_bytes, response_bytes_len);

		RIFT_S_ERROR("Failed to read controller config block - only got %d bytes\n%s", response_bytes_len, buf);
		return;
	}
	response_bytes += 5;

	ctrl->config.accel_limit = READ_LE16(response_bytes + 0);
	ctrl->config.gyro_limit = READ_LE16(response_bytes + 2);
	ctrl->config.accel_hz = READ_LE16(response_bytes + 4);
	ctrl->config.gyro_hz = READ_LE16(response_bytes + 6);
	ctrl->config.accel_scale = READ_LEFLOAT32(response_bytes + 8);
	ctrl->config.gyro_scale = READ_LEFLOAT32(response_bytes + 12);

	ctrl->have_config = true;

	RIFT_S_INFO("Read config for controller 0x%16" PRIx64
	            " type %08x. "
	            "limit/scale/hz Accel %u %f %u Gyro %u %f %u",
	            ctrl->device_id, ctrl->device_type, ctrl->config.accel_limit, ctrl->config.accel_scale,
	            ctrl->config.accel_hz, ctrl->config.gyro_limit, ctrl->config.gyro_scale, ctrl->config.gyro_hz);
}

static void
ctrl_json_cb(bool success, uint8_t *response_bytes, int response_bytes_len, struct rift_s_controller *ctrl)
{
	ctrl->reading_calibration = false;

	if (!success) {
		RIFT_S_WARN("Failed to read controller calibration block");
		return;
	}

	RIFT_S_TRACE("Got Controller calibration:\n%s", response_bytes);

	if (rift_s_controller_parse_imu_calibration((char *)response_bytes, &ctrl->calibration) == 0) {
		ctrl->have_calibration = true;
	} else {
		RIFT_S_ERROR("Failed to parse controller configuration for controller 0x%16" PRIx64 "\n",
		             ctrl->device_id);
	}
}

static void
rift_s_update_input_bool(struct rift_s_controller *ctrl, int index, int64_t when_ns, int val)
{
	ctrl->base.inputs[index].timestamp = when_ns;
	ctrl->base.inputs[index].value.boolean = (val != 0);
}

static void
rift_s_update_input_analog(struct rift_s_controller *ctrl, int index, int64_t when_ns, float val)
{
	ctrl->base.inputs[index].timestamp = when_ns;
	ctrl->base.inputs[index].value.vec1.x = val;
}

static void
rift_s_update_input_vec2(struct rift_s_controller *ctrl, int index, int64_t when_ns, float x, float y)
{
	ctrl->base.inputs[index].timestamp = when_ns;
	ctrl->base.inputs[index].value.vec2.x = x;
	ctrl->base.inputs[index].value.vec2.y = y;
}

static void
rift_s_controller_update_inputs(struct xrt_device *xdev)
{
	struct rift_s_controller *ctrl = (struct rift_s_controller *)(xdev);

	os_mutex_lock(&ctrl->mutex);

	uint64_t last_ns = ctrl->last_controls_local_time_ns;

	if (ctrl->device_type == RIFT_S_DEVICE_LEFT_CONTROLLER) {
		rift_s_update_input_bool(ctrl, OCULUS_TOUCH_X_CLICK, last_ns, ctrl->buttons & RIFT_S_BUTTON_A_X);
		rift_s_update_input_bool(ctrl, OCULUS_TOUCH_Y_CLICK, last_ns, ctrl->buttons & RIFT_S_BUTTON_B_Y);
		rift_s_update_input_bool(ctrl, OCULUS_TOUCH_MENU_CLICK, last_ns,
		                         ctrl->buttons & RIFT_S_BUTTON_MENU_OCULUS);
		rift_s_update_input_bool(
		    ctrl, OCULUS_TOUCH_X_TOUCH, last_ns,
		    !!((ctrl->fingers & RIFT_S_FINGER_A_X_STRONG) ||
		       ((ctrl->fingers & RIFT_S_FINGER_A_X_WEAK) &&
		        !(ctrl->fingers & (RIFT_S_FINGER_B_Y_STRONG | RIFT_S_FINGER_STICK_STRONG)))));
		rift_s_update_input_bool(
		    ctrl, OCULUS_TOUCH_Y_TOUCH, last_ns,
		    !!((ctrl->fingers & RIFT_S_FINGER_B_Y_STRONG) ||
		       ((ctrl->fingers & RIFT_S_FINGER_B_Y_WEAK) &&
		        !(ctrl->fingers & (RIFT_S_FINGER_A_X_STRONG | RIFT_S_FINGER_STICK_STRONG)))));
	} else {
		rift_s_update_input_bool(ctrl, OCULUS_TOUCH_A_CLICK, last_ns, ctrl->buttons & RIFT_S_BUTTON_A_X);
		rift_s_update_input_bool(ctrl, OCULUS_TOUCH_B_CLICK, last_ns, ctrl->buttons & RIFT_S_BUTTON_B_Y);
		rift_s_update_input_bool(ctrl, OCULUS_TOUCH_SYSTEM_CLICK, last_ns,
		                         ctrl->buttons & RIFT_S_BUTTON_MENU_OCULUS);
		rift_s_update_input_bool(
		    ctrl, OCULUS_TOUCH_A_TOUCH, last_ns,
		    !!((ctrl->fingers & RIFT_S_FINGER_A_X_STRONG) ||
		       ((ctrl->fingers & RIFT_S_FINGER_A_X_WEAK) &&
		        !(ctrl->fingers & (RIFT_S_FINGER_B_Y_STRONG | RIFT_S_FINGER_STICK_STRONG)))));
		rift_s_update_input_bool(
		    ctrl, OCULUS_TOUCH_B_TOUCH, last_ns,
		    !!((ctrl->fingers & RIFT_S_FINGER_B_Y_STRONG) ||
		       ((ctrl->fingers & RIFT_S_FINGER_B_Y_WEAK) &&
		        !(ctrl->fingers & (RIFT_S_FINGER_A_X_STRONG | RIFT_S_FINGER_STICK_STRONG)))));
	}

	rift_s_update_input_analog(ctrl, OCULUS_TOUCH_SQUEEZE_VALUE, last_ns, 1.0 - (float)(ctrl->grip) / 4096.0);
	rift_s_update_input_analog(ctrl, OCULUS_TOUCH_TRIGGER_VALUE, last_ns, 1.0 - (float)(ctrl->trigger) / 4096.0);

	rift_s_update_input_bool(ctrl, OCULUS_TOUCH_TRIGGER_TOUCH, last_ns,
	                         !!(ctrl->fingers & (RIFT_S_FINGER_TRIGGER_WEAK | RIFT_S_FINGER_TRIGGER_STRONG)));

	rift_s_update_input_bool(ctrl, OCULUS_TOUCH_THUMBSTICK_CLICK, last_ns, ctrl->buttons & RIFT_S_BUTTON_STICK);

	rift_s_update_input_bool(ctrl, OCULUS_TOUCH_THUMBSTICK_TOUCH, last_ns,
	                         !!((ctrl->fingers & RIFT_S_FINGER_STICK_STRONG) ||
	                            ((ctrl->fingers & RIFT_S_FINGER_STICK_WEAK) &&
	                             !(ctrl->fingers & (RIFT_S_FINGER_A_X_STRONG | RIFT_S_FINGER_B_Y_STRONG)))));

	rift_s_update_input_vec2(ctrl, OCULUS_TOUCH_THUMBSTICK, last_ns,
	                         (float)(ctrl->joystick_x) / 32768.0, /* FIXME: Scale this properly */
	                         (float)(ctrl->joystick_y) / 32768.0  /* FIXME: Scale this properly */
	);

	/* FIXME: Output touch detections:
	      OCULUS_TOUCH_THUMBREST_TOUCH, - does Rift S have a thumbrest?
	*/

	os_mutex_unlock(&ctrl->mutex);
}

static void
rift_s_controller_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	/* TODO: Implement haptic sending */
}

static void
rift_s_controller_get_fusion_pose(struct rift_s_controller *ctrl,
                                  enum xrt_input_name name,
                                  uint64_t at_timestamp_ns,
                                  struct xrt_space_relation *out_relation)
{
	out_relation->pose = ctrl->pose;
	out_relation->linear_velocity.x = 0.0f;
	out_relation->linear_velocity.y = 0.0f;
	out_relation->linear_velocity.z = 0.0f;

	/*!
	 * @todo This is hack, fusion reports angvel relative to the device but
	 * it needs to be in relation to the base space. Rotating it with the
	 * device orientation is enough to get it into the right space, angular
	 * velocity is a derivative so needs a special rotation.
	 */
	math_quat_rotate_derivative(&ctrl->pose.orientation, &ctrl->fusion.last.gyro, &out_relation->angular_velocity);

	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
	    XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT | XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT);
}

static void
rift_s_controller_get_tracked_pose(struct xrt_device *xdev,
                                   enum xrt_input_name name,
                                   uint64_t at_timestamp_ns,
                                   struct xrt_space_relation *out_relation)
{
	struct rift_s_controller *ctrl = (struct rift_s_controller *)(xdev);

	if (name != XRT_INPUT_TOUCH_AIM_POSE && name != XRT_INPUT_TOUCH_GRIP_POSE) {
		RIFT_S_ERROR("unknown pose name requested");
		return;
	}

	struct xrt_relation_chain xrc = {0};

	struct xrt_pose pose_correction = {0};

	/* Rotate the grip/aim pose up by 40 degrees around the X axis */
	struct xrt_vec3 axis = {1.0, 0, 0};

	math_quat_from_angle_vector(DEG_TO_RAD(40), &axis, &pose_correction.orientation);

	m_relation_chain_push_pose(&xrc, &pose_correction);

	/* Apply the fusion rotation */
	struct xrt_space_relation *rel = m_relation_chain_reserve(&xrc);

	os_mutex_lock(&ctrl->mutex);
	rift_s_controller_get_fusion_pose(ctrl, name, at_timestamp_ns, rel);
	os_mutex_unlock(&ctrl->mutex);

	m_relation_chain_resolve(&xrc, out_relation);
}

static void
rift_s_controller_get_view_poses(struct xrt_device *xdev,
                                 const struct xrt_vec3 *default_eye_relation,
                                 uint64_t at_timestamp_ns,
                                 uint32_t view_count,
                                 struct xrt_space_relation *out_head_relation,
                                 struct xrt_fov *out_fovs,
                                 struct xrt_pose *out_poses)
{
	u_device_get_view_poses(xdev, default_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
	                        out_poses);
}

static void
rift_s_controller_destroy(struct xrt_device *xdev)
{
	struct rift_s_controller *ctrl = (struct rift_s_controller *)(xdev);

	/* Tell the system this controller is going away */
	rift_s_system_remove_controller(ctrl->sys, ctrl);

	/* Release the HMD reference */
	rift_s_system_reference(&ctrl->sys, NULL);

	u_var_remove_root(ctrl);

	m_imu_3dof_close(&ctrl->fusion);

	os_mutex_destroy(&ctrl->mutex);

	u_device_free(&ctrl->base);
}

struct rift_s_controller *
rift_s_controller_create(struct rift_s_system *sys, enum xrt_device_type device_type)
{
	DRV_TRACE_MARKER();

	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)(U_DEVICE_ALLOC_TRACKING_NONE);

	struct rift_s_controller *ctrl = U_DEVICE_ALLOCATE(struct rift_s_controller, flags, INPUT_INDICES_LAST, 1);
	if (ctrl == NULL) {
		return NULL;
	}

	/* Store a ref to the parent hmd, released in destroy */
	rift_s_system_reference(&ctrl->sys, sys);

	os_mutex_init(&ctrl->mutex);

	ctrl->base.update_inputs = rift_s_controller_update_inputs;
	ctrl->base.set_output = rift_s_controller_set_output;
	ctrl->base.get_tracked_pose = rift_s_controller_get_tracked_pose;
	ctrl->base.get_view_poses = rift_s_controller_get_view_poses;
	ctrl->base.destroy = rift_s_controller_destroy;
	ctrl->base.name = XRT_DEVICE_TOUCH_CONTROLLER;
	ctrl->base.device_type = device_type;

	if (device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) {
		ctrl->device_type = RIFT_S_DEVICE_LEFT_CONTROLLER;
	} else {
		ctrl->device_type = RIFT_S_DEVICE_RIGHT_CONTROLLER;
	}

	ctrl->pose.orientation.w = 1.0f; // All other values set to zero by U_DEVICE_ALLOCATE (which calls U_CALLOC)
	m_imu_3dof_init(&ctrl->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	// Setup inputs and outputs
	if (device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) {
		snprintf(ctrl->base.str, XRT_DEVICE_NAME_LEN, "Oculus Rift S Left Touch Controller");
		snprintf(ctrl->base.serial, XRT_DEVICE_NAME_LEN, "Left Controller");
		SET_TOUCH_INPUT(ctrl, X_CLICK);
		SET_TOUCH_INPUT(ctrl, X_TOUCH);
		SET_TOUCH_INPUT(ctrl, Y_CLICK);
		SET_TOUCH_INPUT(ctrl, Y_TOUCH);
		SET_TOUCH_INPUT(ctrl, MENU_CLICK);
	} else {
		snprintf(ctrl->base.str, XRT_DEVICE_NAME_LEN, "Oculus Rift S Right Touch Controller");
		snprintf(ctrl->base.serial, XRT_DEVICE_NAME_LEN, "Right Controller");
		SET_TOUCH_INPUT(ctrl, A_CLICK);
		SET_TOUCH_INPUT(ctrl, A_TOUCH);
		SET_TOUCH_INPUT(ctrl, B_CLICK);
		SET_TOUCH_INPUT(ctrl, B_TOUCH);
		SET_TOUCH_INPUT(ctrl, SYSTEM_CLICK);
	}

	SET_TOUCH_INPUT(ctrl, SQUEEZE_VALUE);
	SET_TOUCH_INPUT(ctrl, TRIGGER_TOUCH);
	SET_TOUCH_INPUT(ctrl, TRIGGER_VALUE);
	SET_TOUCH_INPUT(ctrl, THUMBSTICK_CLICK);
	SET_TOUCH_INPUT(ctrl, THUMBSTICK_TOUCH);
	SET_TOUCH_INPUT(ctrl, THUMBSTICK);
	SET_TOUCH_INPUT(ctrl, THUMBREST_TOUCH);
	SET_TOUCH_INPUT(ctrl, GRIP_POSE);
	SET_TOUCH_INPUT(ctrl, AIM_POSE);

	ctrl->base.outputs[0].name = XRT_OUTPUT_NAME_TOUCH_HAPTIC;

	ctrl->base.binding_profiles = binding_profiles_rift_s;
	ctrl->base.binding_profile_count = ARRAY_SIZE(binding_profiles_rift_s);

	u_var_add_root(ctrl, ctrl->base.str, true);
	u_var_add_gui_header(ctrl, NULL, "Tracking");
	u_var_add_pose(ctrl, &ctrl->pose, "Tracked Pose");

	u_var_add_gui_header(ctrl, NULL, "3DoF Tracking");
	m_imu_3dof_add_vars(&ctrl->fusion, ctrl, "");

	u_var_add_gui_header(ctrl, NULL, "Controls");
	if (device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) {
		DEBUG_TOUCH_INPUT_BOOL(ctrl, X_CLICK, "X button");
		DEBUG_TOUCH_INPUT_BOOL(ctrl, X_TOUCH, "X button touch");
		DEBUG_TOUCH_INPUT_BOOL(ctrl, Y_CLICK, "Y button");
		DEBUG_TOUCH_INPUT_BOOL(ctrl, Y_TOUCH, "Y button touch");
		DEBUG_TOUCH_INPUT_BOOL(ctrl, MENU_CLICK, "Menu button");
	} else {
		DEBUG_TOUCH_INPUT_BOOL(ctrl, A_CLICK, "A button");
		DEBUG_TOUCH_INPUT_BOOL(ctrl, A_TOUCH, "A button touch");
		DEBUG_TOUCH_INPUT_BOOL(ctrl, B_CLICK, "B button");
		DEBUG_TOUCH_INPUT_BOOL(ctrl, B_TOUCH, "B button touch");
		DEBUG_TOUCH_INPUT_BOOL(ctrl, SYSTEM_CLICK, "Oculus button");
	}

	DEBUG_TOUCH_INPUT_F32(ctrl, SQUEEZE_VALUE, "Grip value");

	DEBUG_TOUCH_INPUT_BOOL(ctrl, TRIGGER_TOUCH, "Trigger touch");
	DEBUG_TOUCH_INPUT_F32(ctrl, TRIGGER_VALUE, "Trigger");
	DEBUG_TOUCH_INPUT_BOOL(ctrl, THUMBSTICK_CLICK, "Thumbstick click");
	DEBUG_TOUCH_INPUT_BOOL(ctrl, THUMBSTICK_TOUCH, "Thumbstick touch");
	DEBUG_TOUCH_INPUT_VEC2(ctrl, THUMBSTICK, "Thumbstick X", "Thumbstick Y");
	DEBUG_TOUCH_INPUT_BOOL(ctrl, THUMBREST_TOUCH, "Thumbrest touch");

	return ctrl;
}

void
rift_s_controller_update_configuration(struct rift_s_controller *ctrl, uint64_t device_id)
{
	rift_s_radio_state *radio = rift_s_system_radio(ctrl->sys);

	if (ctrl->device_id != device_id) {
		ctrl->device_id = device_id;
		snprintf(ctrl->base.serial, XRT_DEVICE_NAME_LEN, "%016" PRIx64, device_id);
		// If the device ID changed somehow, re-read the JSON blocks
		ctrl->have_config = ctrl->have_calibration = false;
	}

	if (!ctrl->have_config && !ctrl->reading_config) {
		const uint8_t config_req[] = {0x32, 0x20, 0xe8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
		rift_s_radio_queue_command(radio, ctrl->device_id, config_req, sizeof(config_req),
		                           (rift_s_radio_completion_fn)ctrl_config_cb, ctrl);
		ctrl->reading_config = true;
	}

	if (!ctrl->have_calibration && !ctrl->reading_calibration) {
		rift_s_radio_get_json_block(radio, ctrl->device_id, (rift_s_radio_completion_fn)ctrl_json_cb, ctrl);
		ctrl->reading_calibration = true;
	}
}
