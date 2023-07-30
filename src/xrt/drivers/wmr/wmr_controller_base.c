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

#include "util/u_file.h"
#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_trace_marker.h"

#include "wmr_common.h"
#include "wmr_controller_base.h"
#include "wmr_config_key.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#define WMR_TRACE(wcb, ...) U_LOG_XDEV_IFL_T(&wcb->base, wcb->log_level, __VA_ARGS__)
#define WMR_TRACE_HEX(wcb, ...) U_LOG_XDEV_IFL_T_HEX(&wcb->base, wcb->log_level, __VA_ARGS__)
#define WMR_DEBUG(wcb, ...) U_LOG_XDEV_IFL_D(&wcb->base, wcb->log_level, __VA_ARGS__)
#define WMR_DEBUG_HEX(wcb, ...) U_LOG_XDEV_IFL_D_HEX(&wcb->base, wcb->log_level, __VA_ARGS__)
#define WMR_INFO(wcb, ...) U_LOG_XDEV_IFL_I(&wcb->base, wcb->log_level, __VA_ARGS__)
#define WMR_WARN(wcb, ...) U_LOG_XDEV_IFL_W(&wcb->base, wcb->log_level, __VA_ARGS__)
#define WMR_ERROR(wcb, ...) U_LOG_XDEV_IFL_E(&wcb->base, wcb->log_level, __VA_ARGS__)

#define wmr_controller_hexdump_buffer(wcb, label, buf, length)                                                         \
	do {                                                                                                           \
		WMR_DEBUG(wcb, "%s", label);                                                                           \
		WMR_DEBUG_HEX(wcb, buf, length);                                                                       \
	} while (0);


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
		bool b = wcb->handle_input_packet(wcb, time_ns, &buffer[1], (size_t)buf_size - 1);
		os_mutex_unlock(&wcb->data_lock);

		if (!b) {
			WMR_ERROR(wcb, "WMR Controller: Failed handling message type: %02x, size: %i", buffer[0],
			          buf_size);
			wmr_controller_hexdump_buffer(wcb, "Controller Message", buffer, buf_size);
			return;
		}

		break;
	default: WMR_DEBUG(wcb, "WMR Controller: Unknown message type: %02x, size: %i", buffer[0], buf_size); break;
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

		if (response->buf[0] == response_code) {
			WMR_TRACE(wcb, "Controller fw read returned %d bytes", size);
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
	wmr_controller_hexdump_buffer(d, "Data block", data, data_size);

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
read_controller_fw_info(struct wmr_controller_base *wcb,
                        uint32_t *fw_revision,
                        uint16_t *calibration_size,
                        char serial_no[16])
{
	uint8_t *data = NULL;
	size_t data_size;
	int ret;

	/* FW block 0 contains the FW revision (offset 0x14, size 4) and
	 * calibration block size (offset 0x34 size 2) */
	ret = wmr_read_fw_block(wcb, 0x0, &data, &data_size);
	if (ret < 0 || data == NULL) {
		WMR_ERROR(wcb, "Failed to read FW info block 0");
		return false;
	}
	if (data_size < 0x36) {
		WMR_ERROR(wcb, "Failed to read FW info block 0 - too short");
		free(data);
		return false;
	}


	const unsigned char *tmp = data + 0x14;
	*fw_revision = read32(&tmp);
	tmp = data + 0x34;
	*calibration_size = read16(&tmp);

	free(data);

	/* FW block 3 contains the controller serial number at offset
	 * 0x84, size 16 bytes */
	ret = wmr_read_fw_block(wcb, 0x3, &data, &data_size);
	if (ret < 0 || data == NULL) {
		WMR_ERROR(wcb, "Failed to read FW info block 3");
		return false;
	}
	if (data_size < 0x94) {
		WMR_ERROR(wcb, "Failed to read FW info block 3 - too short");
		free(data);
		return false;
	}

	memcpy(serial_no, data + 0x84, 0x10);
	serial_no[16] = '\0';

	free(data);
	return true;
}

char *
build_cache_filename(char *serial_no)
{
	int outlen = strlen("controller-") + strlen(serial_no) + strlen(".json") + 1;
	char *out = malloc(outlen);
	int ret = snprintf(out, outlen, "controller-%s.json", serial_no);

	assert(ret <= outlen);
	(void)ret;

	// Make sure the filename is valid
	for (char *cur = out; *cur != '\0'; cur++) {
		if (!isalnum(*cur) && *cur != '.') {
			*cur = '_';
		}
	}

	return out;
}

static bool
read_calibration_cache(struct wmr_controller_base *wcb, char *cache_filename)
{
	FILE *f = u_file_open_file_in_config_dir_subpath("wmr", cache_filename, "r");
	uint8_t *buffer = NULL;

	if (f == NULL) {
		WMR_DEBUG(wcb, "Failed to open wmr/%s cache file or it doesn't exist.", cache_filename);
		return false;
	}

	// Read the file size to allocate a read buffer
	fseek(f, 0L, SEEK_END);
	size_t file_size = ftell(f);

	// Reset and read the data
	fseek(f, 0L, SEEK_SET);

	buffer = calloc(file_size + 1, sizeof(uint8_t));
	if (buffer == NULL) {
		goto fail;
	}
	buffer[file_size] = '\0';

	size_t ret = fread(buffer, sizeof(char), file_size, f);
	if (ret != file_size) {
		WMR_WARN(wcb, "Cache file wmr/%s failed to read %u bytes (got %u)", cache_filename, (int)file_size,
		         (int)ret);
		goto fail;
	}

	if (!wmr_controller_config_parse(&wcb->config, (char *)buffer, wcb->log_level)) {
		WMR_WARN(wcb, "Cache file wmr/%s contains invalid JSON. Ignoring", cache_filename);
		goto fail;
	}

	fclose(f);
	free(buffer);

	return true;

fail:
	if (buffer) {
		free(buffer);
	}
	fclose(f);
	return false;
}

static void
write_calibration_cache(struct wmr_controller_base *wcb, char *cache_filename, uint8_t *data, size_t data_size)
{
	FILE *f = u_file_open_file_in_config_dir_subpath("wmr", cache_filename, "w");
	if (f == NULL) {
		return;
	}

	size_t ret = fwrite(data, sizeof(char), data_size, f);
	if (ret != data_size) {
		fclose(f);
		return;
	}

	fclose(f);
}

static bool
read_controller_config(struct wmr_controller_base *wcb)
{
	unsigned char *config_json_block;
	int ret;
	uint32_t fw_revision;
	uint16_t calibration_size;
	char serial_no[16 + 1];

	if (!read_controller_fw_info(wcb, &fw_revision, &calibration_size, serial_no)) {
		return false;
	}

	WMR_INFO(wcb, "Reading configuration for controller serial %s. FW revision %x", serial_no, fw_revision);

#if 0
  /* WMR also reads block 0x14, which seems to have some FW revision info,
   * but we don't use it */
	// Read block 0x14
	ret = wmr_read_fw_block(wcb, 0x14, &data, &data_size);
	if (ret < 0 || data == NULL)
		return false;
	free(data);
	data = NULL;
#endif

	// Read config block
	WMR_INFO(wcb, "Reading %s controller config",
	         wcb->base.device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER ? "left" : "right");

	// Check if we have it cached already
	char *cache_filename = build_cache_filename(serial_no);

	if (!read_calibration_cache(wcb, cache_filename)) {
		unsigned char *data = NULL;
		size_t data_size;

		ret = wmr_read_fw_block(wcb, 0x02, &data, &data_size);
		if (ret < 0 || data == NULL || data_size < 2) {
			free(cache_filename);
			return false;
		}

		/* De-obfuscate the JSON config */
		config_json_block = data + sizeof(uint16_t);
		for (unsigned int i = 0; i < data_size - sizeof(uint16_t); i++) {
			config_json_block[i] ^= wmr_config_key[i % sizeof(wmr_config_key)];
		}

		if (!wmr_controller_config_parse(&wcb->config, (char *)config_json_block, wcb->log_level)) {
			free(cache_filename);
			free(data);
			return false;
		}

		/* Write to the cache file (if it fails, ignore it, it's just a cache) */
		write_calibration_cache(wcb, cache_filename, config_json_block, data_size - sizeof(uint16_t));
		free(data);
	} else {
		WMR_DEBUG(wcb, "Read %s controller config from cache %s",
		          wcb->base.device_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER ? "left" : "right",
		          cache_filename);
	}
	free(cache_filename);

	WMR_DEBUG(wcb, "Parsed %d LED entries from controller calibration", wcb->config.led_count);

	return true;
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

void
wmr_controller_base_deinit(struct wmr_controller_base *wcb)
{
	DRV_TRACE_MARKER();

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
}

/*
 *
 * 'Exported' functions.
 *
 */

bool
wmr_controller_base_init(struct wmr_controller_base *wcb,
                         struct wmr_controller_connection *conn,
                         enum xrt_device_type controller_type,
                         enum u_logging_level log_level)
{
	DRV_TRACE_MARKER();

	wcb->log_level = log_level;
	wcb->wcc = conn;
	wcb->receive_bytes = receive_bytes;

	if (controller_type == XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER) {
		snprintf(wcb->base.str, ARRAY_SIZE(wcb->base.str), "WMR Left Controller");
		/* TODO: use proper serial from read_controller_config()? */
		snprintf(wcb->base.serial, XRT_DEVICE_NAME_LEN, "Left Controller");
	} else {
		snprintf(wcb->base.str, ARRAY_SIZE(wcb->base.str), "WMR Right Controller");
		/* TODO: use proper serial from read_controller_config()? */
		snprintf(wcb->base.serial, XRT_DEVICE_NAME_LEN, "Right Controller");
	}

	wcb->base.get_tracked_pose = wmr_controller_base_get_tracked_pose;

	wcb->base.name = XRT_DEVICE_WMR_CONTROLLER;
	wcb->base.device_type = controller_type;
	wcb->base.orientation_tracking_supported = true;
	wcb->base.position_tracking_supported = false;
	wcb->base.hand_tracking_supported = false;

	m_imu_3dof_init(&wcb->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	if (os_mutex_init(&wcb->conn_lock) != 0 || os_mutex_init(&wcb->data_lock) != 0) {
		WMR_ERROR(wcb, "WMR Controller: Failed to init mutex!");
		return false;
	}

	u_var_add_root(wcb, wcb->base.str, true);

	/* Send init commands */
	struct wmr_controller_fw_cmd fw_cmd = {
	    0,
	};
	struct wmr_controller_fw_cmd_response fw_cmd_response;

	/* Zero command. Reinits controller internal state */
	fw_cmd = WMR_CONTROLLER_FW_CMD_INIT(0x06, 0x0, 0, 0);
	if (wmr_controller_send_fw_cmd(wcb, &fw_cmd, 0x06, &fw_cmd_response) < 0) {
		return false;
	}

	/* Quiesce/restart controller tasks */
	fw_cmd = WMR_CONTROLLER_FW_CMD_INIT(0x06, 0x04, 0xc1, 0x02);
	if (wmr_controller_send_fw_cmd(wcb, &fw_cmd, 0x06, &fw_cmd_response) < 0) {
		return false;
	}

	// Read config file from controller
	if (!read_controller_config(wcb)) {
		return false;
	}

	wmr_config_precompute_transforms(&wcb->config.sensors, NULL);

	/* Enable the status reports, IMU and control status reports */
	const unsigned char wmr_controller_status_enable_cmd[64] = {0x06, 0x03, 0x01, 0x00, 0x02};
	wmr_controller_send_bytes(wcb, wmr_controller_status_enable_cmd, sizeof(wmr_controller_status_enable_cmd));
	const unsigned char wmr_controller_imu_on_cmd[64] = {0x06, 0x03, 0x02, 0xe1, 0x02};
	wmr_controller_send_bytes(wcb, wmr_controller_imu_on_cmd, sizeof(wmr_controller_imu_on_cmd));

	return true;
}
