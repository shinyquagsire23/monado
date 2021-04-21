// Copyright 2018, Philipp Zabel.
// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Driver code for a WMR HMD.
 * @author Philipp Zabel <philipp.zabel@gmail.com>
 * @author nima01 <nima_zero_one@protonmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_wmr
 */

#include "xrt/xrt_config_os.h"
#include "xrt/xrt_device.h"

#include "os/os_time.h"
#include "os/os_hid.h"

#include "math/m_mathinclude.h"
#include "math/m_api.h"
#include "math/m_vec2.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_distortion_mesh.h"

#include "wmr_hmd.h"
#include "wmr_common.h"
#include "wmr_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifndef XRT_OS_WINDOWS
#include <unistd.h> // for sleep()
#endif


/*
 *
 * Hololens packets.
 *
 */

static void
hololens_unknown_17_decode_packet(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	if (size >= 7) {
		WMR_TRACE(wh, "Got packet 0x17 (%i)\n\t%02x %02x %02x %02x %02x %02x %02x ", size, buffer[0], buffer[1],
		          buffer[2], buffer[3], buffer[4], buffer[5], buffer[6]);
	} else {
		WMR_TRACE(wh, "Got packet 0x17 (%i)", size);
	}
}

static void
hololens_unknown_05_06_0E_decode_packet(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	if (size >= 45) {
		WMR_TRACE(wh,
		          "Got controller (%i)\n\t%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x | %02x %02x %02x "
		          "%02x %02x %02x %02x %02x %02x %02x | %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		          size, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7],
		          buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15],
		          buffer[16], buffer[17], buffer[18], buffer[19], buffer[20], buffer[21], buffer[22],
		          buffer[23], buffer[24], buffer[25], buffer[26], buffer[27], buffer[28], buffer[29]);
	} else {
		WMR_TRACE(wh, "Got controller packet (%i)\n\t%02x", size, buffer[0]);
	}
}

static void
hololens_sensors_decode_packet(struct wmr_hmd *wh,
                               struct hololens_sensors_packet *pkt,
                               const unsigned char *buffer,
                               int size)
{
	WMR_TRACE(wh, "");

	if (size != 497 && size != 381) {
		WMR_ERROR(wh, "invalid hololens sensor packet size (expected 381 or 497 but got %d)", size);
		return;
	}

	pkt->id = read8(&buffer);
	for (int i = 0; i < 4; i++) {
		pkt->temperature[i] = read16(&buffer);
	}

	for (int i = 0; i < 4; i++) {
		pkt->gyro_timestamp[i] = read64(&buffer);
	}

	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 32; j++) {
			pkt->gyro[i][j] = read16(&buffer);
		}
	}

	for (int i = 0; i < 4; i++) {
		pkt->accel_timestamp[i] = read64(&buffer);
	}

	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 4; j++) {
			pkt->accel[i][j] = read32(&buffer);
		}
	}

	for (int i = 0; i < 4; i++) {
		pkt->video_timestamp[i] = read64(&buffer);
	}

	return;
}

static bool
hololens_sensors_read_packets(struct wmr_hmd *wh)
{
	WMR_TRACE(wh, "");

	unsigned char buffer[WMR_FEATURE_BUFFER_SIZE];

	// Block for 100ms
	int size = os_hid_read(wh->hid_hololens_senors_dev, buffer, sizeof(buffer), 100);

	if (size < 0) {
		WMR_ERROR(wh, "Error reading from device");
		return false;
	} else if (size == 0) {
		WMR_TRACE(wh, "No more data to read");
		return true; // No more messages, return.
	} else {
		WMR_TRACE(wh, "Read %u bytes", size);
	}

	switch (buffer[0]) {
	case WMR_MS_HOLOLENS_MSG_SENSORS:
		hololens_sensors_decode_packet(wh, &wh->packet, buffer, size);

		for (int i = 0; i < 4; i++) {
			vec3_from_hololens_gyro(wh->packet.gyro, i, &wh->raw_gyro);
			vec3_from_hololens_accel(wh->packet.accel, i, &wh->raw_accel);

			os_mutex_lock(&wh->fusion_mutex);
			m_imu_3dof_update(&wh->fusion, wh->packet.gyro_timestamp[i] * WMR_MS_HOLOLENS_NS_PER_TICK,
			                  &wh->raw_accel, &wh->raw_gyro);
			os_mutex_unlock(&wh->fusion_mutex);
		}
		break;
	case WMR_MS_HOLOLENS_MSG_UNKNOWN_05:
	case WMR_MS_HOLOLENS_MSG_UNKNOWN_06:
	case WMR_MS_HOLOLENS_MSG_UNKNOWN_0E: //
		hololens_unknown_05_06_0E_decode_packet(wh, buffer, size);
		break;
	case WMR_MS_HOLOLENS_MSG_UNKNOWN_17: //
		hololens_unknown_17_decode_packet(wh, buffer, size);
		break;
	case WMR_MS_HOLOLENS_MSG_CONTROL:
	case WMR_MS_HOLOLENS_MSG_DEBUG: //
		break;
	default: //
		WMR_DEBUG(wh, "Unknown message type: %02x, (%i)", buffer[0], size);
		break;
	}

	return true;
}


/*
 *
 * Control packets.
 *
 */

static void
control_ipd_value_decode(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	if (size != 4) {
		WMR_ERROR(wh, "Invalid control ipd distance packet size (expected 4 but got %i)", size);
		return;
	}

	uint8_t id = read8(&buffer);
	uint8_t unknown = read8(&buffer);
	uint16_t value = read16(&buffer);

	(void)id;
	(void)unknown;

	wh->raw_ipd = value;

	WMR_DEBUG(wh, "Got IPD value: %04x", value);
}

static bool
control_read_packets(struct wmr_hmd *wh)
{
	unsigned char buffer[WMR_FEATURE_BUFFER_SIZE];

	// Do not block
	int size = os_hid_read(wh->hid_control_dev, buffer, sizeof(buffer), 0);

	if (size < 0) {
		WMR_ERROR(wh, "Error reading from device");
		return false;
	} else if (size == 0) {
		WMR_TRACE(wh, "No more data to read");
		return true; // No more messages, return.
	} else {
		WMR_TRACE(wh, "Read %u bytes", size);
	}

	switch (buffer[0]) {
	case WMR_CONTROL_MSG_IPD_VALUE: //
		control_ipd_value_decode(wh, buffer, size);
		break;
	case WMR_CONTROL_MSG_UNKNOWN_05: //
		break;
	default: //
		WMR_DEBUG(wh, "Unknown message type: %02x, (%i)", buffer[0], size);
		break;
	}

	return true;
}


/*
 *
 * Helpers and internal functions.
 *
 */

static void *
wmr_run_thread(void *ptr)
{
	struct wmr_hmd *wh = (struct wmr_hmd *)ptr;

	os_thread_helper_lock(&wh->oth);
	while (os_thread_helper_is_running_locked(&wh->oth)) {
		os_thread_helper_unlock(&wh->oth);

		// Does not block.
		if (!control_read_packets(wh)) {
			break;
		}

		// Does block for a bit.
		if (!hololens_sensors_read_packets(wh)) {
			break;
		}
	}

	WMR_DEBUG(wh, "Exiting reading thread.");

	return NULL;
}

static void
hololens_sensors_enable_imu(struct wmr_hmd *wh)
{
	int size = os_hid_write(wh->hid_hololens_senors_dev, hololens_sensors_imu_on, sizeof(hololens_sensors_imu_on));
	if (size <= 0) {
		WMR_ERROR(wh, "Error writing to device");
		return;
	}
}

#define HID_SEND(HID, DATA, STR)                                                                                       \
	do {                                                                                                           \
		int _ret = os_hid_set_feature(HID, DATA, sizeof(DATA));                                                \
		if (_ret < 0) {                                                                                        \
			WMR_ERROR(wh, "Send (%s): %i", STR, _ret);                                                     \
		}                                                                                                      \
	} while (false);

#define HID_GET(HID, DATA, STR)                                                                                        \
	do {                                                                                                           \
		int _ret = os_hid_get_feature(HID, DATA[0], DATA, sizeof(DATA));                                       \
		if (_ret < 0) {                                                                                        \
			WMR_ERROR(wh, "Get (%s): %i", STR, _ret);                                                      \
		}                                                                                                      \
	} while (false);

static int
wmr_hmd_activate(struct wmr_hmd *wh)
{
	struct os_hid_device *hid = wh->hid_control_dev;

	WMR_TRACE(wh, "Activating HP Reverb G1/G2 HMD...");


	// Hack to power up the Reverb G1 display, thanks to OpenHMD contibutors.
	// Sleep before we start seems to improve reliability.
	// 300ms is what Windows seems to do, so cargo cult that.
	os_nanosleep(U_TIME_1MS_IN_NS * 300);

	for (int i = 0; i < 4; i++) {
		unsigned char cmd[64] = {0x50, 0x01};
		HID_SEND(hid, cmd, "loop");

		unsigned char data[64] = {0x50};
		HID_GET(hid, data, "loop");

		os_nanosleep(U_TIME_1MS_IN_NS * 10); // Sleep 10ms
	}

	unsigned char data[64] = {0x09};
	HID_GET(hid, data, "data_1");

	data[0] = 0x08;
	HID_GET(hid, data, "data_2");

	data[0] = 0x06;
	HID_GET(hid, data, "data_3");

	// Wake up the display.
	unsigned char cmd[2] = {0x04, 0x01};
	HID_SEND(hid, cmd, "screen_on");

	WMR_INFO(wh, "Sent activation report, sleeping for compositor.");

	/*
	 * Sleep so display completes power up and modes be enumerated.
	 * Two seconds seems to be needed, 1 was not enough.
	 */
	os_nanosleep(U_TIME_1MS_IN_NS * 2000);


	return 0;
}

static void
wmr_hmd_deactivate(struct wmr_hmd *wh)
{
	struct os_hid_device *hid = wh->hid_control_dev;

	/* Turn the screen off */
	unsigned char cmd[2] = {0x04, 0x00};
	HID_SEND(hid, cmd, "screen_off");
}

static void
wmr_hmd_update_inputs(struct xrt_device *xdev)
{
	struct wmr_hmd *wh = wmr_hmd(xdev);
	(void)wh;
}

static void
wmr_hmd_get_tracked_pose(struct xrt_device *xdev,
                         enum xrt_input_name name,
                         uint64_t at_timestamp_ns,
                         struct xrt_space_relation *out_relation)
{
	struct wmr_hmd *wh = wmr_hmd(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		WMR_ERROR(wh, "Unknown input name");
		return;
	}

	// Clear relation.
	U_ZERO(out_relation);

	os_mutex_lock(&wh->fusion_mutex);
	out_relation->pose.orientation = wh->fusion.rot;
	os_mutex_unlock(&wh->fusion_mutex);

	out_relation->relation_flags = (enum xrt_space_relation_flags)( //
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |                  //
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
}

static void
wmr_hmd_get_view_pose(struct xrt_device *xdev,
                      struct xrt_vec3 *eye_relation,
                      uint32_t view_index,
                      struct xrt_pose *out_pose)
{
	struct xrt_pose pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
	bool adjust = (view_index == 0);

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

static void
wmr_hmd_destroy(struct xrt_device *xdev)
{
	struct wmr_hmd *wh = wmr_hmd(xdev);

	// Destroy the thread object.
	os_thread_helper_destroy(&wh->oth);

	if (wh->hid_hololens_senors_dev != NULL) {
		os_hid_destroy(wh->hid_hololens_senors_dev);
		wh->hid_hololens_senors_dev = NULL;
	}

	if (wh->hid_control_dev != NULL) {
		wmr_hmd_deactivate(wh);
		os_hid_destroy(wh->hid_control_dev);
		wh->hid_control_dev = NULL;
	}

	// Destroy the fusion.
	m_imu_3dof_close(&wh->fusion);

	os_mutex_destroy(&wh->fusion_mutex);

	free(wh);
}

struct xrt_device *
wmr_hmd_create(struct os_hid_device *hid_holo, struct os_hid_device *hid_ctrl, enum u_logging_level ll)
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	int ret = 0;

	struct wmr_hmd *wh = U_DEVICE_ALLOCATE(struct wmr_hmd, flags, 1, 0);
	if (!wh) {
		return NULL;
	}

	// Populate the base members.
	wh->base.update_inputs = wmr_hmd_update_inputs;
	wh->base.get_tracked_pose = wmr_hmd_get_tracked_pose;
	wh->base.get_view_pose = wmr_hmd_get_view_pose;
	wh->base.destroy = wmr_hmd_destroy;
	wh->base.name = XRT_DEVICE_GENERIC_HMD;
	wh->base.device_type = XRT_DEVICE_TYPE_HMD;
	wh->log_level = ll;

	wh->base.orientation_tracking_supported = true;
	wh->base.position_tracking_supported = false;
	wh->base.hand_tracking_supported = false;
	wh->hid_hololens_senors_dev = hid_holo;
	wh->hid_control_dev = hid_ctrl;

	snprintf(wh->base.str, XRT_DEVICE_NAME_LEN, "HP Reverb VR Headset");

	// Mutex before thread.
	ret = os_mutex_init(&wh->fusion_mutex);
	if (ret != 0) {
		WMR_ERROR(wh, "Failed to init mutex!");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return NULL;
	}

	// Thread and other state.
	ret = os_thread_helper_init(&wh->oth);
	if (ret != 0) {
		WMR_ERROR(wh, "Failed to init threading!");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return NULL;
	}

	if (wmr_hmd_activate(wh) != 0) {
		WMR_ERROR(wh, "Activation of HMD failed");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return NULL;
	}

	// Switch on IMU on the HMD.
	hololens_sensors_enable_imu(wh);

	// Setup input.
	wh->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	// TODO: Read config file from HMD, provide guestimate values for now.
	struct u_device_simple_info info;
	info.display.w_pixels = 4320;
	info.display.h_pixels = 2160;
	info.display.w_meters = 0.13f;
	info.display.h_meters = 0.07f;
	info.lens_horizontal_separation_meters = 0.13f / 2.0f;
	info.lens_vertical_position_meters = 0.07f / 2.0f;
	info.views[0].fov = 85.0f * (M_PI / 180.0f);
	info.views[1].fov = 85.0f * (M_PI / 180.0f);

	if (!u_device_setup_split_side_by_side(&wh->base, &info)) {
		WMR_ERROR(wh, "Failed to setup basic HMD device info");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return NULL;
	}

	m_imu_3dof_init(&wh->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	// Setup variable tracker.
	u_var_add_root(wh, "WMR HMD", true);
	u_var_add_gui_header(wh, &wh->gui.fusion, "3DoF Fusion");
	m_imu_3dof_add_vars(&wh->fusion, wh, "");
	u_var_add_gui_header(wh, &wh->gui.misc, "Misc");
	u_var_add_log_level(wh, &wh->log_level, "log_level");

	// Distortion information, fills in xdev->compute_distortion().
	u_distortion_mesh_set_none(&wh->base);

	// Hand over hololens sensor device to reading thread.
	ret = os_thread_helper_start(&wh->oth, wmr_run_thread, wh);
	if (ret != 0) {
		WMR_ERROR(wh, "Failed to start thread!");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return NULL;
	}

	return &wh->base;
}
