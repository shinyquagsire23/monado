// Copyright 2020-2021, N Madsen.
// Copyright 2020-2023, Collabora, Ltd.
// Copyright 2020-2023, Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Driver for WMR Controller.
 * @author Jan Schmidt <jan@centricular.com>
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
#include "util/u_trace_marker.h"

#include "wmr_common.h"
#include "wmr_controller_base.h"
#include "wmr_config_key.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#define WMR_TRACE(wcb, ...) U_LOG_XDEV_IFL_T(&wcb->base, wcb->log_level, __VA_ARGS__)
#define WMR_DEBUG(wcb, ...) U_LOG_XDEV_IFL_D(&wcb->base, wcb->log_level, __VA_ARGS__)
#define WMR_INFO(wcb, ...) U_LOG_XDEV_IFL_I(&wcb->base, wcb->log_level, __VA_ARGS__)
#define WMR_WARN(wcb, ...) U_LOG_XDEV_IFL_W(&wcb->base, wcb->log_level, __VA_ARGS__)
#define WMR_ERROR(wcb, ...) U_LOG_XDEV_IFL_E(&wcb->base, wcb->log_level, __VA_ARGS__)

/*!
 * Indices in input list of each input.
 */
enum wmr_bt_input_index
{
	WMR_INDEX_MENU_CLICK,
	WMR_INDEX_SQUEEZE_CLICK,
	WMR_INDEX_TRIGGER_VALUE,
	WMR_INDEX_THUMBSTICK_CLICK,
	WMR_INDEX_THUMBSTICK,
	WMR_INDEX_TRACKPAD_CLICK,
	WMR_INDEX_TRACKPAD_TOUCH,
	WMR_INDEX_TRACKPAD,
	WMR_INDEX_GRIP_POSE,
	WMR_INDEX_AIM_POSE,
};

#define SET_INPUT(NAME) (wcb->base.inputs[WMR_INDEX_##NAME].name = XRT_INPUT_WMR_##NAME)

//! file path to store controller JSON configuration blocks that
//! read from the firmware.
DEBUG_GET_ONCE_OPTION(wmr_ctrl_config_path, "WMR_CONFIG_DUMP", NULL)

static inline struct wmr_controller_base *
wmr_controller_base(struct xrt_device *p)
{
	return (struct wmr_controller_base *)p;
}

static void
receive_bytes(struct wmr_controller_base *wcb, uint64_t time_ns, uint8_t *buffer, uint32_t buf_size)
{
	if (buf_size < 1) {
		WMR_ERROR(wcb, "WMR Controller: Error receiving short packet");
		return;
	}

	switch (buffer[0]) {
	case WMR_MOTION_CONTROLLER_STATUS_MSG:
		os_mutex_lock(&wcb->data_lock);
		// Note: skipping msg type byte
		bool b = wmr_controller_packet_parse(&buffer[1], (size_t)buf_size - 1, &wcb->input, wcb->log_level);
		if (b) {
			m_imu_3dof_update(&wcb->fusion,
			                  wcb->input.imu.timestamp_ticks * WMR_MOTION_CONTROLLER_NS_PER_TICK,
			                  &wcb->input.imu.acc, &wcb->input.imu.gyro);

			wcb->last_imu_timestamp_ns = time_ns;
			wcb->last_angular_velocity = wcb->input.imu.gyro;

		} else {
			WMR_ERROR(wcb, "WMR Controller (Bluetooth): Failed parsing message type: %02x, size: %i",
			          buffer[0], buf_size);
			os_mutex_unlock(&wcb->data_lock);
			return;
		}
		os_mutex_unlock(&wcb->data_lock);
		break;
	default:
		WMR_DEBUG(wcb, "WMR Controller (Bluetooth): Unknown message type: %02x, size: %i", buffer[0], buf_size);
		break;
	}

	return;
}

static bool
wmr_controller_send_bytes(struct wmr_controller_base *wcb, const uint8_t *buffer, uint32_t buf_size)
{
	bool res = false;

	os_mutex_lock(&wcb->conn_lock);
	struct wmr_controller_connection *conn = wcb->wcc;
	if (conn != NULL) {
		res = wmr_controller_connection_send_bytes(conn, buffer, buf_size);
	}
	os_mutex_unlock(&wcb->conn_lock);

	return res;
}

static int
wmr_controller_read_sync(struct wmr_controller_base *wcb, uint8_t *buffer, uint32_t buf_size, int timeout_ms)
{
	int res = -1;
	os_mutex_lock(&wcb->conn_lock);
	struct wmr_controller_connection *conn = wcb->wcc;
	if (conn != NULL) {
		res = wmr_controller_connection_read_sync(conn, buffer, buf_size, timeout_ms);
	}
	os_mutex_unlock(&wcb->conn_lock);

	return res;
}

static int
wmr_controller_send_fw_cmd(struct wmr_controller_base *wcb,
                           const struct wmr_controller_fw_cmd *fw_cmd,
                           unsigned char response_code,
                           struct wmr_controller_fw_cmd_response *response)
{
	// comms timeout. Replies are usually in 10ms or so but the first can take longer
	const int timeout_ms = 250;
	const int timeout_ns = timeout_ms * U_TIME_1MS_IN_NS;
	uint64_t timeout_start = os_monotonic_get_ns();
	uint64_t timeout_end_ns = timeout_start + timeout_ns;

	if (!wmr_controller_send_bytes(wcb, fw_cmd->buf, sizeof(fw_cmd->buf))) {
		return -1;
	}

	do {
		int size = wmr_controller_read_sync(wcb, response->buf, sizeof(response->buf), timeout_ms);
		if (size == -1) {
			return -1;
		}

		if (size < 1) {
			// Ignore 0-byte reads (timeout) and try again
			continue;
		}

		WMR_TRACE(wcb, "Controller fw read returned %d bytes", size);
		if (response->buf[0] == response_code) {
			if (size != sizeof(response->buf) || (response->response.cmd_id_echo != fw_cmd->cmd.cmd_id)) {
				WMR_DEBUG(
				    wcb, "Unexpected fw response - size %d (expected %zu), cmd_id_echo %u != cmd_id %u",
				    size, sizeof(response->buf), response->response.cmd_id_echo, fw_cmd->cmd.cmd_id);
				return -1;
			}

			response->response.blk_remain = __le32_to_cpu(response->response.blk_remain);
			return size;
		}
	} while (os_monotonic_get_ns() < timeout_end_ns);

	WMR_WARN(wcb, "Controller fw read timed out after %u ms",
	         (unsigned int)((os_monotonic_get_ns() - timeout_start) / U_TIME_1MS_IN_NS));
	return -ETIMEDOUT;
}

XRT_MAYBE_UNUSED static int
wmr_read_fw_block(struct wmr_controller_base *d, uint8_t blk_id, uint8_t **out_data, size_t *out_size)
{
	struct wmr_controller_fw_cmd_response fw_cmd_response;

	uint8_t *data;
	uint8_t *data_pos;
	uint8_t *data_end;
	uint32_t data_size;
	uint32_t remain;

	struct wmr_controller_fw_cmd fw_cmd;
	memset(&fw_cmd, 0, sizeof(fw_cmd));

	fw_cmd = WMR_CONTROLLER_FW_CMD_INIT(0x06, 0x02, blk_id, 0xffffffff);
	if (wmr_controller_send_fw_cmd(d, &fw_cmd, 0x02, &fw_cmd_response) < 0) {
		WMR_WARN(d, "Failed to read fw - cmd 0x02 failed to read header for block %d", blk_id);
		return -1;
	}

	data_size = fw_cmd_response.response.blk_remain + fw_cmd_response.response.len;
	WMR_DEBUG(d, "FW header %d bytes, %u bytes in block", fw_cmd_response.response.len, data_size);
	if (data_size == 0)
		return -1;

	data = calloc(1, data_size + 1);
	if (!data) {
		return -1;
	}
	data[data_size] = '\0';

	remain = data_size;
	data_pos = data;
	data_end = data + data_size;

	uint8_t to_copy = fw_cmd_response.response.len;

	memcpy(data_pos, fw_cmd_response.response.data, to_copy);
	data_pos += to_copy;
	remain -= to_copy;

	while (remain > 0) {
		fw_cmd = WMR_CONTROLLER_FW_CMD_INIT(0x06, 0x02, blk_id, remain);

		os_nanosleep(U_TIME_1MS_IN_NS * 10); // Sleep 10ms
		if (wmr_controller_send_fw_cmd(d, &fw_cmd, 0x02, &fw_cmd_response) < 0) {
			WMR_WARN(d, "Failed to read fw - cmd 0x02 failed @ offset %zu", data_pos - data);
			return -1;
		}

		uint8_t to_copy = fw_cmd_response.response.len;
		if (data_pos + to_copy > data_end)
			to_copy = data_end - data_pos;

		WMR_DEBUG(d, "Read %d bytes @ offset %zu / %d", to_copy, data_pos - data, data_size);
		memcpy(data_pos, fw_cmd_response.response.data, to_copy);
		data_pos += to_copy;
		remain -= to_copy;
	}

	WMR_DEBUG(d, "Read %d-byte FW data block %d", data_size, blk_id);

	*out_data = data;
	*out_size = data_size;

	return 0;
}

/*
 *
 * Config functions.
 *
 */

static bool
read_controller_config(struct wmr_controller_base *wcb)
{
	unsigned char *data = NULL;
	unsigned char *config_json_block;
	size_t data_size;
	int ret;

#if 1
	// There are extra firmware blocks that can be read from
	// the controllers, like these. Serial numbers and
	// USB PID/VID are visible in them, but it's not clear
	// what the layout is and we don't use them currently,
	// so this if 0 code is just exemplary.

	// Read 0x00 block
	ret = wmr_read_fw_block(wcb, 0x0, &data, &data_size);
	if (ret < 0 || data == NULL)
		return false;
	free(data);
	data = NULL;

	// Read serials
	ret = wmr_read_fw_block(wcb, 0x03, &data, &data_size);
	if (ret < 0 || data == NULL)
		return false;
	free(data);
	data = NULL;

	// Read block 0x14
	ret = wmr_read_fw_block(wcb, 0x14, &data, &data_size);
	if (ret < 0 || data == NULL)
		return false;
	free(data);
	data = NULL;
#endif

	// Read config block
	ret = wmr_read_fw_block(wcb, 0x02, &data, &data_size);
	if (ret < 0 || data == NULL)
		return false;

	/* De-obfuscate the JSON config */
	config_json_block = data + sizeof(uint16_t);
	for (unsigned int i = 0; i < data_size - sizeof(uint16_t); i++) {
		config_json_block[i] ^= wmr_config_key[i % sizeof(wmr_config_key)];
	}

#if 1
	// Option to dump config block to a path. Later, these will be
	// stored in a cache to save time on future startup
	const char *dump_dir = debug_get_option_wmr_ctrl_config_path();
	if (dump_dir != NULL) {
		char fname[256];

		int device_id = (wcb->base.device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) ? 0 : 1;

		sprintf(fname, "%s/controller-%d-fw.txt", dump_dir, device_id);
		WMR_INFO(wcb, "Storing controller config JSON to %s", fname);

		FILE *f = fopen(fname, "w");
		fwrite(config_json_block, data_size - 2, 1, f);
		fclose(f);
	}
#endif

	if (!wmr_controller_config_parse(&wcb->config, (char *)config_json_block, wcb->log_level)) {
		free(data);
		return false;
	}

	WMR_DEBUG(wcb, "Parsed %d LED entries from controller calibration", wcb->config.led_count);

	free(data);
	return true;
}

static void
wmr_controller_base_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	DRV_TRACE_MARKER();

	// struct wmr_controller_base *d = wmr_controller_base(xdev);
	// Todo: implement
}

static void
wmr_controller_base_get_tracked_pose(struct xrt_device *xdev,
                                     enum xrt_input_name name,
                                     uint64_t at_timestamp_ns,
                                     struct xrt_space_relation *out_relation)
{
	DRV_TRACE_MARKER();

	struct wmr_controller_base *wcb = wmr_controller_base(xdev);

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
	os_mutex_lock(&wcb->data_lock);
	relation.pose.orientation = wcb->fusion.rot;
	relation.angular_velocity = wcb->last_angular_velocity;
	last_imu_timestamp_ns = wcb->last_imu_timestamp_ns;
	os_mutex_unlock(&wcb->data_lock);

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
wmr_controller_base_update_inputs(struct xrt_device *xdev)
{
	DRV_TRACE_MARKER();

	struct wmr_controller_base *wcb = wmr_controller_base(xdev);

	struct xrt_input *inputs = wcb->base.inputs;

	os_mutex_lock(&wcb->data_lock);

	inputs[WMR_INDEX_MENU_CLICK].value.boolean = wcb->input.menu;
	inputs[WMR_INDEX_SQUEEZE_CLICK].value.boolean = wcb->input.squeeze;
	inputs[WMR_INDEX_TRIGGER_VALUE].value.vec1.x = wcb->input.trigger;
	inputs[WMR_INDEX_THUMBSTICK_CLICK].value.boolean = wcb->input.thumbstick.click;
	inputs[WMR_INDEX_THUMBSTICK].value.vec2 = wcb->input.thumbstick.values;
	inputs[WMR_INDEX_TRACKPAD_CLICK].value.boolean = wcb->input.trackpad.click;
	inputs[WMR_INDEX_TRACKPAD_TOUCH].value.boolean = wcb->input.trackpad.touch;
	inputs[WMR_INDEX_TRACKPAD].value.vec2 = wcb->input.trackpad.values;

	os_mutex_unlock(&wcb->data_lock);
}

static void
wmr_controller_base_destroy(struct xrt_device *xdev)
{
	DRV_TRACE_MARKER();

	struct wmr_controller_base *wcb = wmr_controller_base(xdev);

	// Remove the variable tracking.
	u_var_remove_root(wcb);

	// Disconnect from the connection so we don't
	// receive any more callbacks
	os_mutex_lock(&wcb->conn_lock);
	struct wmr_controller_connection *conn = wcb->wcc;
	wcb->wcc = NULL;
	os_mutex_unlock(&wcb->conn_lock);

	if (conn != NULL) {
		wmr_controller_connection_disconnect(conn);
	}

	os_mutex_destroy(&wcb->conn_lock);
	os_mutex_destroy(&wcb->data_lock);

	// Destroy the fusion.
	m_imu_3dof_close(&wcb->fusion);

	free(wcb);
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

struct wmr_controller_base *
wmr_controller_base_create(struct wmr_controller_connection *conn,
                           enum xrt_device_type controller_type,
                           enum u_logging_level log_level)
{
	DRV_TRACE_MARKER();

	enum u_device_alloc_flags flags = U_DEVICE_ALLOC_TRACKING_NONE;
	struct wmr_controller_base *wcb = U_DEVICE_ALLOCATE(struct wmr_controller_base, flags, 10, 1);

	wcb->log_level = log_level;
	wcb->wcc = conn;
	wcb->receive_bytes = receive_bytes;

	if (controller_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) {
		snprintf(wcb->base.str, ARRAY_SIZE(wcb->base.str), "WMR Left Controller");
	} else {
		snprintf(wcb->base.str, ARRAY_SIZE(wcb->base.str), "WMR Right Controller");
	}

	wcb->base.destroy = wmr_controller_base_destroy;
	wcb->base.get_tracked_pose = wmr_controller_base_get_tracked_pose;
	wcb->base.set_output = wmr_controller_base_set_output;
	wcb->base.update_inputs = wmr_controller_base_update_inputs;

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

	for (uint32_t i = 0; i < wcb->base.input_count; i++) {
		wcb->base.inputs[0].active = true;
	}

	wcb->base.outputs[0].name = XRT_OUTPUT_NAME_WMR_HAPTIC;

	wcb->base.binding_profiles = binding_profiles;
	wcb->base.binding_profile_count = ARRAY_SIZE(binding_profiles);

	wcb->base.name = XRT_DEVICE_WMR_CONTROLLER;
	wcb->base.device_type = controller_type;
	wcb->base.orientation_tracking_supported = true;
	wcb->base.position_tracking_supported = false;
	wcb->base.hand_tracking_supported = true;


	wcb->input.imu.timestamp_ticks = 0;
	m_imu_3dof_init(&wcb->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	if (os_mutex_init(&wcb->conn_lock) != 0 || os_mutex_init(&wcb->data_lock) != 0) {
		WMR_ERROR(wcb, "WMR Controller: Failed to init mutex!");
		wmr_controller_base_destroy(&wcb->base);
		return NULL;
	}

	// Read config file from controller
	if (!read_controller_config(wcb)) {
		wmr_controller_base_destroy(&wcb->base);
		return NULL;
	}

	u_var_add_root(wcb, wcb->base.str, true);
	u_var_add_bool(wcb, &wcb->input.menu, "input.menu");
	u_var_add_bool(wcb, &wcb->input.home, "input.home");
	u_var_add_bool(wcb, &wcb->input.bt_pairing, "input.bt_pairing");
	u_var_add_bool(wcb, &wcb->input.squeeze, "input.squeeze");
	u_var_add_f32(wcb, &wcb->input.trigger, "input.trigger");
	u_var_add_u8(wcb, &wcb->input.battery, "input.battery");
	u_var_add_bool(wcb, &wcb->input.thumbstick.click, "input.thumbstick.click");
	u_var_add_f32(wcb, &wcb->input.thumbstick.values.x, "input.thumbstick.values.y");
	u_var_add_f32(wcb, &wcb->input.thumbstick.values.y, "input.thumbstick.values.x");
	u_var_add_bool(wcb, &wcb->input.trackpad.click, "input.trackpad.click");
	u_var_add_bool(wcb, &wcb->input.trackpad.touch, "input.trackpad.touch");
	u_var_add_f32(wcb, &wcb->input.trackpad.values.x, "input.trackpad.values.x");
	u_var_add_f32(wcb, &wcb->input.trackpad.values.y, "input.trackpad.values.y");
	u_var_add_ro_vec3_f32(wcb, &wcb->input.imu.acc, "imu.acc");
	u_var_add_ro_vec3_f32(wcb, &wcb->input.imu.gyro, "imu.gyro");
	u_var_add_i32(wcb, &wcb->input.imu.temperature, "imu.temperature");

	return wcb;
}
