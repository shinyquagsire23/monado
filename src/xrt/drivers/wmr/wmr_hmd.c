// Copyright 2018, Philipp Zabel.
// Copyright 2020-2021, N Madsen.
// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Driver code for a WMR HMD.
 * @author Philipp Zabel <philipp.zabel@gmail.com>
 * @author nima01 <nima_zero_one@protonmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @author Nova King <technobaboo@proton.me>
 * @ingroup drv_wmr
 */

#include "xrt/xrt_config_drivers.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_build.h"
#include "xrt/xrt_config_os.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_tracking.h"

#include "os/os_time.h"
#include "os/os_hid.h"

#include "math/m_api.h"
#include "math/m_mathinclude.h"
#include "math/m_predict.h"
#include "math/m_vec2.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_trace_marker.h"
#include "util/u_distortion_mesh.h"
#include "util/u_sink.h"

#include "tracking/t_tracking.h"

#include "wmr_hmd.h"
#include "wmr_common.h"
#include "wmr_config_key.h"
#include "wmr_protocol.h"
#include "wmr_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifndef XRT_OS_WINDOWS
#include <unistd.h> // for sleep()
#endif

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
#include "../multi_wrapper/multi.h"
#include "../drivers/ht/ht_interface.h"
#endif

// Unsure if these can change nor how to get them if so
#define CAMERA_FREQUENCY 30      //!< Observed value (OV7251)
#define IMU_FREQUENCY 1000       //!< Observed value (ICM20602)
#define IMU_SAMPLES_PER_PACKET 4 //!< There are 4 samples for each USB IMU packet

//! Specifies whether the user wants to use a SLAM tracker.
DEBUG_GET_ONCE_BOOL_OPTION(wmr_slam, "WMR_SLAM", true)

//! Specifies whether the user wants to use the hand tracker.
DEBUG_GET_ONCE_BOOL_OPTION(wmr_handtracking, "WMR_HANDTRACKING", true)

#ifdef XRT_FEATURE_SLAM
//! Whether to submit samples to the SLAM tracker from the start.
DEBUG_GET_ONCE_OPTION(slam_submit_from_start, "SLAM_SUBMIT_FROM_START", NULL)
#endif

static int
wmr_hmd_activate_reverb(struct wmr_hmd *wh);
static void
wmr_hmd_deactivate_reverb(struct wmr_hmd *wh);
static void
wmr_hmd_screen_enable_reverb(struct wmr_hmd *wh, bool enable);
static int
wmr_hmd_activate_odyssey_plus(struct wmr_hmd *wh);
static void
wmr_hmd_deactivate_odyssey_plus(struct wmr_hmd *wh);
static void
wmr_hmd_screen_enable_odyssey_plus(struct wmr_hmd *wh, bool enable);

const struct wmr_headset_descriptor headset_map[] = {
    {WMR_HEADSET_GENERIC, NULL, "Unknown WMR HMD", NULL, NULL, NULL}, /* Catch-all for unknown headsets */
    {WMR_HEADSET_HP_VR1000, "HP Reverb VR Headset VR1000-1xxx", "HP VR1000", NULL, NULL, NULL}, /*! @todo init funcs */
    {WMR_HEADSET_REVERB_G1, "HP Reverb VR Headset VR1000-2xxx", "HP Reverb", wmr_hmd_activate_reverb,
     wmr_hmd_deactivate_reverb, wmr_hmd_screen_enable_reverb},
    {WMR_HEADSET_REVERB_G2, "HP Reverb Virtual Reality Headset G2", "HP Reverb G2", wmr_hmd_activate_reverb,
     wmr_hmd_deactivate_reverb, wmr_hmd_screen_enable_reverb},
    {WMR_HEADSET_SAMSUNG_XE700X3AI, "Samsung Windows Mixed Reality XE700X3AI", "Samsung Odyssey",
     wmr_hmd_activate_odyssey_plus, wmr_hmd_deactivate_odyssey_plus, wmr_hmd_screen_enable_odyssey_plus},
    {WMR_HEADSET_SAMSUNG_800ZAA, "Samsung Windows Mixed Reality 800ZAA", "Samsung Odyssey+",
     wmr_hmd_activate_odyssey_plus, wmr_hmd_deactivate_odyssey_plus, wmr_hmd_screen_enable_odyssey_plus},
    {WMR_HEADSET_LENOVO_EXPLORER, "Lenovo VR-2511N", "Lenovo Explorer", NULL, NULL, NULL},
    {WMR_HEADSET_MEDION_ERAZER_X1000, "Medion Erazer X1000", "Medion Erazer", NULL, NULL, NULL},
};
const int headset_map_n = sizeof(headset_map) / sizeof(headset_map[0]);


/*
 *
 * Hololens decode packets.
 *
 */

static void
hololens_sensors_decode_packet(struct wmr_hmd *wh,
                               struct hololens_sensors_packet *pkt,
                               const unsigned char *buffer,
                               int size)
{
	WMR_TRACE(wh, " ");

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
}


/*
 *
 * Hololens packets.
 *
 */

static void
hololens_handle_unknown(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	DRV_TRACE_MARKER();

	WMR_DEBUG(wh, "Unknown hololens sensors message type: %02x, (%i)", buffer[0], size);
}

static void
hololens_handle_control(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	DRV_TRACE_MARKER();

	WMR_DEBUG(wh, "WMR_MS_HOLOLENS_MSG_CONTROL: %02x, (%i)", buffer[0], size);
}

static void
hololens_handle_controller_status_packet(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	DRV_TRACE_MARKER();

	if (size < 3) {
		WMR_DEBUG(wh, "Got small packet 0x17 (%i)", size);
		return;
	}

	uint8_t controller_id = buffer[1];
	uint8_t pkt_type = buffer[2];

	switch (pkt_type) {
	case WMR_CONTROLLER_STATUS_UNPAIRED: {
		WMR_TRACE(wh, "Controller %d is not paired", controller_id);
		break;
	}
	case WMR_CONTROLLER_STATUS_OFFLINE: {
		if (size < 7) {
			WMR_TRACE(wh, "Got small controller offline status packet (%i)", size);
			return;
		}

		/* Skip packet type, controller id, presence */
		buffer += 3;

		uint16_t vid = read16(&buffer);
		uint16_t pid = read16(&buffer);
		WMR_TRACE(wh, "Controller %d offline. VID 0x%04x PID 0x%04x", controller_id, vid, pid);
		break;
	}
	case WMR_CONTROLLER_STATUS_ONLINE: {
		if (size < 10) {
			WMR_TRACE(wh, "Got small controller online status packet (%i)", size);
			return;
		}

		/* Skip packet type, controller id, presence */
		buffer += 3;

		uint16_t vid = read16(&buffer);
		uint16_t pid = read16(&buffer);
		uint8_t unknown1 = read8(&buffer);
		uint16_t unknown2160 = read16(&buffer);

		WMR_TRACE(wh, "Controller %d online. VID 0x%04x PID 0x%04x val1 %u val2 %u", controller_id, vid, pid,
		          unknown1, unknown2160);
		break;
	}
	default: //
		WMR_DEBUG(wh, "Unknown controller status packet (%i) type 0x%02x", size, pkt_type);
		break;
	}
}

static void
hololens_handle_bt_iface_packet(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	DRV_TRACE_MARKER();

	int pkt_type;

	if (size < 2)
		return;

	if (size < 6) {
		WMR_DEBUG(wh, "Short Bluetooth interface packet (%d) type 0x%02x", size, buffer[1]);
		return;
	}

	pkt_type = buffer[1];
	if (pkt_type != WMR_BT_IFACE_MSG_DEBUG) {
		WMR_DEBUG(wh, "Unknown Bluetooth interface packet (%d) type 0x%02x", size, pkt_type);
		return;
	}
	buffer += 2;

	uint16_t tag = read16(&buffer);
	uint16_t msg_len = read16(&buffer);

	if (size < msg_len + 6) {
		WMR_DEBUG(wh, "Bluetooth interface debug packet (%d) too short. tag 0x%x msg len %u", size, tag,
		          msg_len);
		return;
	}

	WMR_DEBUG(wh, "BT debug: tag %d: %.*s", tag, msg_len, buffer);
}

static void
hololens_handle_controller_packet(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	DRV_TRACE_MARKER();

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
hololens_handle_debug(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	DRV_TRACE_MARKER();

	if (size < 12) {
		WMR_TRACE(wh, "Got short debug packet (%i) 0x%02x", size, buffer[0]);
		return;
	}
	buffer += 1;

	uint32_t magic = read32(&buffer);
	if (magic != WMR_MAGIC) {
		WMR_TRACE(wh, "Debug packet (%i) 0x%02x had strange magic 0x%08x", size, buffer[0], magic);
		return;
	}
	uint32_t timestamp = read32(&buffer);
	uint16_t seq = read16(&buffer);
	uint8_t src_tag = read8(&buffer);
	int msg_len = size - 12;

	WMR_DEBUG(wh, "HMD debug: TS %f seq %u src %d: %.*s", timestamp / 1000.0, seq, src_tag, msg_len, buffer);
}

static void
hololens_handle_sensors_avg(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	DRV_TRACE_MARKER();

	// Get the timing as close to reading the packet as possible.
	uint64_t now_ns = os_monotonic_get_ns();

	hololens_sensors_decode_packet(wh, &wh->packet, buffer, size);

	// Use a single averaged sample from all the samples in the packet
	struct xrt_vec3 avg_raw_accel = XRT_VEC3_ZERO;
	struct xrt_vec3 avg_raw_gyro = XRT_VEC3_ZERO;
	for (int i = 0; i < IMU_SAMPLES_PER_PACKET; i++) {
		struct xrt_vec3 a = XRT_VEC3_ZERO;
		struct xrt_vec3 g = XRT_VEC3_ZERO;
		vec3_from_hololens_accel(wh->packet.accel, i, &a);
		vec3_from_hololens_gyro(wh->packet.gyro, i, &g);
		math_vec3_accum(&a, &avg_raw_accel);
		math_vec3_accum(&g, &avg_raw_gyro);
	}
	math_vec3_scalar_mul(1.0f / IMU_SAMPLES_PER_PACKET, &avg_raw_accel);
	math_vec3_scalar_mul(1.0f / IMU_SAMPLES_PER_PACKET, &avg_raw_gyro);

	// Calibrate averaged sample
	struct xrt_vec3 avg_calib_accel = XRT_VEC3_ZERO;
	struct xrt_vec3 avg_calib_gyro = XRT_VEC3_ZERO;
	math_matrix_3x3_transform_vec3(&wh->config.sensors.accel.mix_matrix, &avg_raw_accel, &avg_calib_accel);
	math_matrix_3x3_transform_vec3(&wh->config.sensors.gyro.mix_matrix, &avg_raw_gyro, &avg_calib_gyro);
	math_vec3_accum(&wh->config.sensors.accel.bias_offsets, &avg_calib_accel);
	math_vec3_accum(&wh->config.sensors.gyro.bias_offsets, &avg_calib_gyro);
	math_quat_rotate_vec3(&wh->P_oxr_acc.orientation, &avg_calib_accel, &avg_calib_accel);
	math_quat_rotate_vec3(&wh->P_oxr_gyr.orientation, &avg_calib_gyro, &avg_calib_gyro);

	// Fusion tracking
	os_mutex_lock(&wh->fusion.mutex);
	timepoint_ns t = wh->packet.gyro_timestamp[IMU_SAMPLES_PER_PACKET - 1] * WMR_MS_HOLOLENS_NS_PER_TICK;
	m_imu_3dof_update(&wh->fusion.i3dof, t, &avg_calib_accel, &avg_calib_gyro);
	wh->fusion.last_imu_timestamp_ns = now_ns;
	wh->fusion.last_angular_velocity = avg_calib_gyro;
	os_mutex_unlock(&wh->fusion.mutex);

	// SLAM tracking
	wmr_source_push_imu_packet(wh->tracking.source, t, avg_raw_accel, avg_raw_gyro);
}

static void
hololens_handle_sensors_all(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	DRV_TRACE_MARKER();

	// Get the timing as close to reading the packet as possible.
	uint64_t now_ns = os_monotonic_get_ns();

	hololens_sensors_decode_packet(wh, &wh->packet, buffer, size);

	struct xrt_vec3 raw_gyro[IMU_SAMPLES_PER_PACKET];
	struct xrt_vec3 raw_accel[IMU_SAMPLES_PER_PACKET];
	struct xrt_vec3 calib_gyro[IMU_SAMPLES_PER_PACKET];
	struct xrt_vec3 calib_accel[IMU_SAMPLES_PER_PACKET];

	for (int i = 0; i < IMU_SAMPLES_PER_PACKET; i++) {
		struct xrt_vec3 *rg = &raw_gyro[i];
		struct xrt_vec3 *cg = &calib_gyro[i];
		vec3_from_hololens_gyro(wh->packet.gyro, i, rg);
		math_matrix_3x3_transform_vec3(&wh->config.sensors.gyro.mix_matrix, rg, cg);
		math_vec3_accum(&wh->config.sensors.gyro.bias_offsets, cg);
		math_quat_rotate_vec3(&wh->P_oxr_gyr.orientation, cg, cg);

		struct xrt_vec3 *ra = &raw_accel[i];
		struct xrt_vec3 *ca = &calib_accel[i];
		vec3_from_hololens_accel(wh->packet.accel, i, ra);
		math_matrix_3x3_transform_vec3(&wh->config.sensors.accel.mix_matrix, ra, ca);
		math_vec3_accum(&wh->config.sensors.accel.bias_offsets, ca);
		math_quat_rotate_vec3(&wh->P_oxr_acc.orientation, ca, ca);
	}

	// Fusion tracking
	os_mutex_lock(&wh->fusion.mutex);
	for (int i = 0; i < IMU_SAMPLES_PER_PACKET; i++) {
		m_imu_3dof_update(                                              //
		    &wh->fusion.i3dof,                                          //
		    wh->packet.gyro_timestamp[i] * WMR_MS_HOLOLENS_NS_PER_TICK, //
		    &calib_accel[i],                                            //
		    &calib_gyro[i]);                                            //
	}
	wh->fusion.last_imu_timestamp_ns = now_ns;
	wh->fusion.last_angular_velocity = calib_gyro[3];
	os_mutex_unlock(&wh->fusion.mutex);

	// SLAM tracking
	for (int i = 0; i < IMU_SAMPLES_PER_PACKET; i++) {
		timepoint_ns t = wh->packet.gyro_timestamp[i] * WMR_MS_HOLOLENS_NS_PER_TICK;
		wmr_source_push_imu_packet(wh->tracking.source, t, raw_accel[i], raw_gyro[i]);
	}
}

static void
hololens_handle_sensors(struct wmr_hmd *wh, const unsigned char *buffer, int size)
{
	if (wh->average_imus) {
		// Less overhead and jitter.
		hololens_handle_sensors_avg(wh, buffer, size);
	} else {
		// More sophisticated fusion algorithms might work better with raw data.
		hololens_handle_sensors_all(wh, buffer, size);
	}
}

static bool
hololens_sensors_read_packets(struct wmr_hmd *wh)
{
	DRV_TRACE_MARKER();

	WMR_TRACE(wh, " ");

	unsigned char buffer[WMR_FEATURE_BUFFER_SIZE];

	// Block for 100ms
	int size = os_hid_read(wh->hid_hololens_sensors_dev, buffer, sizeof(buffer), 100);

	if (size < 0) {
		WMR_ERROR(wh, "Error reading from Hololens Sensors device. Call to os_hid_read returned %i", size);
		return false;
	}
	if (size == 0) {
		WMR_TRACE(wh, "No more data to read");
		return true; // No more messages, return.
	} else {
		WMR_TRACE(wh, "Read %u bytes", size);
	}

	switch (buffer[0]) {
	case WMR_MS_HOLOLENS_MSG_SENSORS: //
		hololens_handle_sensors(wh, buffer, size);
		break;
	case WMR_MS_HOLOLENS_MSG_BT_IFACE: //
		hololens_handle_bt_iface_packet(wh, buffer, size);
		break;
	case WMR_MS_HOLOLENS_MSG_LEFT_CONTROLLER:
	case WMR_MS_HOLOLENS_MSG_RIGHT_CONTROLLER: //
		hololens_handle_controller_packet(wh, buffer, size);
		break;
	case WMR_MS_HOLOLENS_MSG_CONTROLLER_STATUS: //
		hololens_handle_controller_status_packet(wh, buffer, size);
		break;
	case WMR_MS_HOLOLENS_MSG_CONTROL: //
		hololens_handle_control(wh, buffer, size);
		break;
	case WMR_MS_HOLOLENS_MSG_DEBUG: //
		hololens_handle_debug(wh, buffer, size);
		break;
	default: //
		hololens_handle_unknown(wh, buffer, size);
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
	if (size != 2 && size != 4) {
		WMR_ERROR(wh, "Invalid control ipd distance packet size (expected 4 but got %i)", size);
		return;
	}

	uint8_t id = read8(&buffer);
	if (id != 0x1) {
		WMR_ERROR(wh, "Invalid control IPD distance packet ID (expected 0x1 but got %u)", id);
		return;
	}

	uint8_t proximity = read8(&buffer);
	uint16_t ipd_value = (size == 4) ? read16(&buffer) : wh->raw_ipd;

	bool changed = (wh->raw_ipd != ipd_value) || (wh->proximity_sensor != proximity);

	wh->raw_ipd = ipd_value;
	wh->proximity_sensor = proximity;

	if (changed) {
		WMR_DEBUG(wh, "Proximity sensor %d IPD: %d", proximity, ipd_value);
	}
}

static bool
control_read_packets(struct wmr_hmd *wh)
{
	DRV_TRACE_MARKER();

	unsigned char buffer[WMR_FEATURE_BUFFER_SIZE];

	// Do not block
	int size = os_hid_read(wh->hid_control_dev, buffer, sizeof(buffer), 0);

	if (size < 0) {
		WMR_ERROR(wh, "Error reading from companion (HMD control) device. Call to os_hid_read returned %i",
		          size);
		return false;
	}
	if (size == 0) {
		WMR_TRACE(wh, "No more data to read");
		return true; // No more messages, return.
	} else {
		WMR_TRACE(wh, "Read %u bytes", size);
	}

	DRV_TRACE_IDENT(control_packet_got);

	switch (buffer[0]) {
	case WMR_CONTROL_MSG_IPD_VALUE: //
		control_ipd_value_decode(wh, buffer, size);
		break;
	case WMR_CONTROL_MSG_UNKNOWN_02: //
		WMR_DEBUG(wh, "Unknown message type: %02x (size %i)", buffer[0], size);
		if (size == 4) {
			// Todo: Decode.
			// On Reverb G1 this message is sometimes received right after a
			// proximity/IPD message, and it always seems to be '02 XX 0d 26'.
			WMR_DEBUG(wh, "---> Type and content bytes: %02x %02x %02x %02x", buffer[0], buffer[1],
			          buffer[2], buffer[3]);
		}
		break;
	case WMR_CONTROL_MSG_DEVICE_STATUS: //
		WMR_DEBUG(wh, "Device status message type: %02x (size %i)", buffer[0], size);
		if (size != 11) {
			WMR_DEBUG(wh, "---> Unexpected message size. Expected 11 bytes incl. message type.");
			break;
		}

		// Todo: HMD state info to be decoded further.
		// On Reverb G1 this message is received twice after having sent an 'enable screen' command to the HMD
		// companion device. The first one is received promptly. The second one is received a few seconds later
		// once the HMD screen backlight visibly powers on.
		// 1st message: '05 00 01 01 00 00 00 00 00 00 00'
		// 2nd message: '05 01 01 01 01 00 00 00 00 00 00'
		WMR_DEBUG(wh, "---> Type and content bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		          buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7],
		          buffer[8], buffer[9], buffer[10]);
		WMR_DEBUG(wh,
		          "---> Flags decoded so far: [type: %02x] [display_ready: %02x] [?] [?] [display_ready: %02x] "
		          "[?] [?] [?] [?] [?] [?]",
		          buffer[0], buffer[1], buffer[4]);

		break;
	default: //
		WMR_DEBUG(wh, "Unknown message type: %02x (size %i)", buffer[0], size);
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
	DRV_TRACE_MARKER();

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
		os_thread_helper_lock(&wh->oth);
	}
	os_thread_helper_unlock(&wh->oth);

	WMR_DEBUG(wh, "Exiting reading thread.");

	return NULL;
}

static void
hololens_sensors_enable_imu(struct wmr_hmd *wh)
{
	DRV_TRACE_MARKER();

	int size = os_hid_write(wh->hid_hololens_sensors_dev, hololens_sensors_imu_on, sizeof(hololens_sensors_imu_on));
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
wmr_hmd_activate_reverb(struct wmr_hmd *wh)
{
	DRV_TRACE_MARKER();

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

	WMR_INFO(wh, "Sent activation report.");

	// Enable the HMD screen now, if required. Otherwise, if screen should initially be disabled, then
	// proactively disable it now. Why? Because some cases of irregular termination of Monado will
	// leave either the 'Hololens Sensors' device or its 'companion' device alive across restarts.
	wmr_hmd_screen_enable_reverb(wh, wh->hmd_screen_enable);

	// Allow time for enumeration of available displays by host system, so the compositor can select among them.
	WMR_INFO(wh,
	         "Sleep until the HMD display is powered up so, the available displays can be enumerated by the host "
	         "system.");

	// Two seconds seems to be needed, 1 was not enough.
	os_nanosleep(U_TIME_1MS_IN_NS * 2000);

	return 0;
}

static void
wmr_hmd_refresh_debug_gui(struct wmr_hmd *wh)
{
	// Update debug GUI button labels.
	if (wh) {
		struct u_var_button *btn = &wh->gui.hmd_screen_enable_btn;
		snprintf(btn->label, sizeof(btn->label),
		         wh->hmd_screen_enable ? "HMD Screen [On]" : "HMD Screen [Off]");
	}
}

static void
wmr_hmd_deactivate_reverb(struct wmr_hmd *wh)
{
	DRV_TRACE_MARKER();

	// Turn the screen off
	wmr_hmd_screen_enable_reverb(wh, false);

	//! @todo Power down IMU, and maybe more.
}

static void
wmr_hmd_screen_enable_reverb(struct wmr_hmd *wh, bool enable)
{
	DRV_TRACE_MARKER();

	struct os_hid_device *hid = wh->hid_control_dev;

	unsigned char cmd[2] = {0x04, 0x00};
	if (enable) {
		cmd[1] = enable ? 0x01 : 0x00;
	}

	HID_SEND(hid, cmd, (enable ? "screen_on" : "screen_off"));

	wh->hmd_screen_enable = enable;

	// Update debug GUI button labels.
	wmr_hmd_refresh_debug_gui(wh);
}

static int
wmr_hmd_activate_odyssey_plus(struct wmr_hmd *wh)
{
	DRV_TRACE_MARKER();

	struct os_hid_device *hid = wh->hid_control_dev;

	WMR_TRACE(wh, "Activating Odyssey HMD...");

	os_nanosleep(U_TIME_1MS_IN_NS * 300);

	unsigned char data[64] = {0x16};
	HID_GET(hid, data, "data_1");

	data[0] = 0x15;
	HID_GET(hid, data, "data_2");

	data[0] = 0x14;
	HID_GET(hid, data, "data_3");

	// Enable the HMD screen now, if required. Otherwise, if screen should initially be disabled, then
	// proactively disable it now. Why? Because some cases of irregular termination of Monado will
	// leave either the 'Hololens Sensors' device or its 'companion' device alive across restarts.
	wmr_hmd_screen_enable_odyssey_plus(wh, wh->hmd_screen_enable);

	// Allow time for enumeration of available displays by host system, so the compositor can select among them.
	WMR_INFO(wh,
	         "Sleep until the HMD display is powered up, so the available displays can be enumerated by the host "
	         "system.");

	os_nanosleep(3L * U_TIME_1S_IN_NS);

	return 0;
}

static void
wmr_hmd_deactivate_odyssey_plus(struct wmr_hmd *wh)
{
	DRV_TRACE_MARKER();

	// Turn the screen off
	wmr_hmd_screen_enable_odyssey_plus(wh, false);

	//! @todo Power down IMU, and maybe more.
}

static void
wmr_hmd_screen_enable_odyssey_plus(struct wmr_hmd *wh, bool enable)
{
	DRV_TRACE_MARKER();

	struct os_hid_device *hid = wh->hid_control_dev;

	unsigned char cmd[2] = {0x12, 0x00};
	if (enable) {
		cmd[1] = enable ? 0x01 : 0x00;
	}

	HID_SEND(hid, cmd, (enable ? "screen_on" : "screen_off"));

	wh->hmd_screen_enable = enable;

	// Update debug GUI button labels.
	wmr_hmd_refresh_debug_gui(wh);
}

static void
wmr_hmd_screen_enable_toggle(void *wh_ptr)
{
	struct wmr_hmd *wh = (struct wmr_hmd *)wh_ptr;
	if (wh && wh->hmd_desc && wh->hmd_desc->screen_enable_func) {
		wh->hmd_desc->screen_enable_func(wh, !wh->hmd_screen_enable);
	}
}

/*
 *
 * Config functions.
 *
 */

static int
wmr_config_command_sync(struct wmr_hmd *wh, unsigned char type, unsigned char *buf, int len)
{
	DRV_TRACE_MARKER();

	struct os_hid_device *hid = wh->hid_hololens_sensors_dev;

	unsigned char cmd[64] = {0x02, type};
	os_hid_write(hid, cmd, sizeof(cmd));

	do {
		int size = os_hid_read(hid, buf, len, -1);
		if (size == -1) {
			return -1;
		}
		if (buf[0] == WMR_MS_HOLOLENS_MSG_CONTROL) {
			return size;
		}
	} while (true);

	return -1;
}

static int
wmr_read_config_part(struct wmr_hmd *wh, unsigned char type, unsigned char *data, int len)
{
	DRV_TRACE_MARKER();

	unsigned char buf[33];
	int offset = 0;
	int size;

	size = wmr_config_command_sync(wh, 0x0b, buf, sizeof(buf));
	if (size != 33 || buf[0] != 0x02) {
		WMR_ERROR(wh, "Failed to issue command 0b: %02x %02x %02x", buf[0], buf[1], buf[2]);
		return -1;
	}

	size = wmr_config_command_sync(wh, type, buf, sizeof(buf));
	if (size != 33 || buf[0] != 0x02) {
		WMR_ERROR(wh, "Failed to issue command %02x: %02x %02x %02x", type, buf[0], buf[1], buf[2]);
		return -1;
	}

	while (true) {
		size = wmr_config_command_sync(wh, 0x08, buf, sizeof(buf));
		if (size != 33 || (buf[1] != 0x01 && buf[1] != 0x02)) {
			WMR_ERROR(wh, "Failed to issue command 08: %02x %02x %02x", buf[0], buf[1], buf[2]);
			return -1;
		}

		if (buf[1] != 0x01) {
			break;
		}

		if (buf[2] > len || offset + buf[2] > len) {
			WMR_ERROR(wh, "Getting more information then requested");
			return -1;
		}

		memcpy(data + offset, buf + 3, buf[2]);
		offset += buf[2];
	}

	return offset;
}

XRT_MAYBE_UNUSED static int
wmr_read_config_raw(struct wmr_hmd *wh, uint8_t **out_data, size_t *out_size)
{
	DRV_TRACE_MARKER();

	unsigned char meta[84];
	uint8_t *data;
	int size;
	int data_size;

	size = wmr_read_config_part(wh, 0x06, meta, sizeof(meta));
	WMR_DEBUG(wh, "(0x06, meta) => %d", size);

	if (size < 0) {
		return -1;
	}

	/*
	 * No idea what the other 64 bytes of metadata are, but the first two
	 * seem to be little endian size of the data store.
	 */
	data_size = meta[0] | (meta[1] << 8);
	data = calloc(1, data_size + 1);
	if (!data) {
		return -1;
	}
	data[data_size] = '\0';

	size = wmr_read_config_part(wh, 0x04, data, data_size);
	WMR_DEBUG(wh, "(0x04, data) => %d", size);
	if (size < 0) {
		free(data);
		return -1;
	}

	WMR_DEBUG(wh, "Read %d-byte config data", data_size);

	*out_data = data;
	*out_size = size;

	return 0;
}

static int
wmr_read_config(struct wmr_hmd *wh)
{
	DRV_TRACE_MARKER();

	unsigned char *data = NULL;
	unsigned char *config_json_block;
	size_t data_size;
	int ret;

	// Read config
	ret = wmr_read_config_raw(wh, &data, &data_size);
	if (ret < 0)
		return ret;

	/* De-obfuscate the JSON config */
	/* FIXME: The header contains little-endian values that need swapping for big-endian */
	struct wmr_config_header *hdr = (struct wmr_config_header *)data;

	/* Take a copy of the header */
	memcpy(&wh->config_hdr, hdr, sizeof(struct wmr_config_header));

	WMR_INFO(wh, "Manufacturer: %.*s", (int)sizeof(hdr->manufacturer), hdr->manufacturer);
	WMR_INFO(wh, "Device: %.*s", (int)sizeof(hdr->device), hdr->device);
	WMR_INFO(wh, "Serial: %.*s", (int)sizeof(hdr->serial), hdr->serial);
	WMR_INFO(wh, "UID: %.*s", (int)sizeof(hdr->uid), hdr->uid);
	WMR_INFO(wh, "Name: %.*s", (int)sizeof(hdr->name), hdr->name);
	WMR_INFO(wh, "Revision: %.*s", (int)sizeof(hdr->revision), hdr->revision);
	WMR_INFO(wh, "Revision Date: %.*s", (int)sizeof(hdr->revision_date), hdr->revision_date);

	snprintf(wh->base.str, XRT_DEVICE_NAME_LEN, "%.*s", (int)sizeof(hdr->name), hdr->name);

	if (hdr->json_start >= data_size || (data_size - hdr->json_start) < hdr->json_size) {
		WMR_ERROR(wh, "Invalid WMR config block - incorrect sizes");
		free(data);
		return -1;
	}

	config_json_block = data + hdr->json_start + sizeof(uint16_t);
	for (unsigned int i = 0; i < hdr->json_size - sizeof(uint16_t); i++) {
		config_json_block[i] ^= wmr_config_key[i % sizeof(wmr_config_key)];
	}

	WMR_DEBUG(wh, "JSON config:\n%s", config_json_block);

	if (!wmr_hmd_config_parse(&wh->config, (char *)config_json_block, wh->log_level)) {
		free(data);
		return -1;
	}

	free(data);
	return 0;
}

/*
 *
 * Device members.
 *
 */

static void
wmr_hmd_update_inputs(struct xrt_device *xdev)
{
	DRV_TRACE_MARKER();

	struct wmr_hmd *wh = wmr_hmd(xdev);
	(void)wh;
}

static void
wmr_hmd_get_3dof_tracked_pose(struct xrt_device *xdev,
                              enum xrt_input_name name,
                              uint64_t at_timestamp_ns,
                              struct xrt_space_relation *out_relation)
{
	DRV_TRACE_MARKER();

	struct wmr_hmd *wh = wmr_hmd(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		WMR_ERROR(wh, "Unknown input name");
		return;
	}

	// Variables needed for prediction.
	uint64_t last_imu_timestamp_ns = 0;
	struct xrt_space_relation relation = {0};
	relation.relation_flags = XRT_SPACE_RELATION_BITMASK_ALL;
	relation.pose.position = wh->pose.position;
	relation.linear_velocity = (struct xrt_vec3){0, 0, 0};

	// Get data while holding the lock.
	os_mutex_lock(&wh->fusion.mutex);
	relation.pose.orientation = wh->fusion.i3dof.rot;
	relation.angular_velocity = wh->fusion.last_angular_velocity;
	last_imu_timestamp_ns = wh->fusion.last_imu_timestamp_ns;
	os_mutex_unlock(&wh->fusion.mutex);

	// No prediction needed.
	if (at_timestamp_ns < last_imu_timestamp_ns) {
		*out_relation = relation;
		return;
	}

	uint64_t prediction_ns = at_timestamp_ns - last_imu_timestamp_ns;
	double prediction_s = time_ns_to_s(prediction_ns);

	m_predict_relation(&relation, prediction_s, out_relation);
	wh->pose = out_relation->pose;
}

//! Specific pose corrections for Basalt and a WMR headset
XRT_MAYBE_UNUSED static inline struct xrt_pose
wmr_hmd_correct_pose_from_basalt(struct xrt_pose pose)
{
	struct xrt_quat q = {0.70710678, 0, 0, 0.70710678};
	math_quat_rotate(&q, &pose.orientation, &pose.orientation);
	math_quat_rotate_vec3(&q, &pose.position, &pose.position);

	// Correct swapped axes
	pose.position.y = -pose.position.y;
	pose.position.z = -pose.position.z;
	pose.orientation.y = -pose.orientation.y;
	pose.orientation.z = -pose.orientation.z;
	return pose;
}

static void
wmr_hmd_get_slam_tracked_pose(struct xrt_device *xdev,
                              enum xrt_input_name name,
                              uint64_t at_timestamp_ns,
                              struct xrt_space_relation *out_relation)
{
	DRV_TRACE_MARKER();

	struct wmr_hmd *wh = wmr_hmd(xdev);
	xrt_tracked_slam_get_tracked_pose(wh->tracking.slam, at_timestamp_ns, out_relation);

	int pose_bits = XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
	bool pose_tracked = out_relation->relation_flags & pose_bits;

	if (pose_tracked) {
#if defined(XRT_HAVE_BASALT_SLAM)
		wh->pose = wmr_hmd_correct_pose_from_basalt(out_relation->pose);
#else
		wh->pose = out_relation->pose;
#endif
	}

	if (wh->tracking.imu2me) {
		math_pose_transform(&wh->pose, &wh->P_imu_me, &wh->pose);
	}

	out_relation->pose = wh->pose;
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
}

static void
wmr_hmd_get_tracked_pose(struct xrt_device *xdev,
                         enum xrt_input_name name,
                         uint64_t at_timestamp_ns,
                         struct xrt_space_relation *out_relation)
{
	DRV_TRACE_MARKER();

	struct wmr_hmd *wh = wmr_hmd(xdev);
	if (wh->tracking.slam_enabled && wh->slam_over_3dof) {
		wmr_hmd_get_slam_tracked_pose(xdev, name, at_timestamp_ns, out_relation);
	} else {
		wmr_hmd_get_3dof_tracked_pose(xdev, name, at_timestamp_ns, out_relation);
	}
	math_pose_transform(&wh->offset, &out_relation->pose, &out_relation->pose);
}

static void
wmr_hmd_get_view_poses(struct xrt_device *xdev,
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
wmr_hmd_destroy(struct xrt_device *xdev)
{
	DRV_TRACE_MARKER();

	struct wmr_hmd *wh = wmr_hmd(xdev);

	// Destroy the thread object.
	os_thread_helper_destroy(&wh->oth);

	if (wh->hid_hololens_sensors_dev != NULL) {
		os_hid_destroy(wh->hid_hololens_sensors_dev);
		wh->hid_hololens_sensors_dev = NULL;
	}

	if (wh->hid_control_dev != NULL) {
		/* Do any deinit if we have a deinit function */
		if (wh->hmd_desc && wh->hmd_desc->deinit_func) {
			wh->hmd_desc->deinit_func(wh);
		}
		os_hid_destroy(wh->hid_control_dev);
		wh->hid_control_dev = NULL;
	}

	// Destroy SLAM source and tracker
	xrt_frame_context_destroy_nodes(&wh->tracking.xfctx);

	// Destroy the fusion.
	m_imu_3dof_close(&wh->fusion.i3dof);

	os_mutex_destroy(&wh->fusion.mutex);

	u_device_free(&wh->base);
}

static bool
compute_distortion_wmr(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	DRV_TRACE_MARKER();

	struct wmr_hmd *wh = wmr_hmd(xdev);

	assert(view == 0 || view == 1);

	const struct wmr_distortion_eye_config *ec = wh->config.eye_params + view;
	struct wmr_hmd_distortion_params *distortion_params = wh->distortion_params + view;

	// Results r/g/b.
	struct xrt_vec2 tc[3];

	// Dear compiler, please vectorize.
	for (int i = 0; i < 3; i++) {
		const struct wmr_distortion_3K *distortion3K = ec->distortion3K + i;

		/* Scale the 0..1 input UV back to pixels relative to the distortion center,
		 * accounting for the right eye starting at X = panel_width / 2.0 */
		struct xrt_vec2 pix_coord = {(u + 1.0f * view) * (ec->display_size.x / 2.0f) -
		                                 distortion3K->eye_center.x,
		                             v * ec->display_size.y - distortion3K->eye_center.y};

		float r2 = m_vec2_dot(pix_coord, pix_coord);
		float k1 = (float)distortion3K->k[0];
		float k2 = (float)distortion3K->k[1];
		float k3 = (float)distortion3K->k[2];

		float d = 1.0f + r2 * (k1 + r2 * (k2 + r2 * k3));

		/* Map the distorted pixel coordinate back to normalised view plane coords using the inverse affine
		 * xform */
		struct xrt_vec3 p = {(pix_coord.x * d + distortion3K->eye_center.x),
		                     (pix_coord.y * d + distortion3K->eye_center.y), 1.0f};
		struct xrt_vec3 vp;
		math_matrix_3x3_transform_vec3(&distortion_params->inv_affine_xform, &p, &vp);

		/* Finally map back to the input texture 0..1 range based on the render FoV (from tex_N_range.x ..
		 * tex_N_range.y) */
		tc[i].x = ((vp.x / vp.z) - distortion_params->tex_x_range.x) /
		          (distortion_params->tex_x_range.y - distortion_params->tex_x_range.x);
		tc[i].y = ((vp.y / vp.z) - distortion_params->tex_y_range.x) /
		          (distortion_params->tex_y_range.y - distortion_params->tex_y_range.x);
	}

	result->r = tc[0];
	result->g = tc[1];
	result->b = tc[2];

	return true;
}

/*
 * Compute the visible area bounds by calculating the X/Y limits of a
 * crosshair through the distortion center, and back-project to the render FoV,
 */
static void
compute_distortion_bounds(struct wmr_hmd *wh,
                          int view,
                          float *out_angle_left,
                          float *out_angle_right,
                          float *out_angle_down,
                          float *out_angle_up)
{
	DRV_TRACE_MARKER();

	assert(view == 0 || view == 1);

	float tanangle_left = 0.0f;
	float tanangle_right = 0.0f;
	float tanangle_up = 0.0f;
	float tanangle_down = 0.0f;

	const struct wmr_distortion_eye_config *ec = wh->config.eye_params + view;
	struct wmr_hmd_distortion_params *distortion_params = wh->distortion_params + view;

	for (int i = 0; i < 3; i++) {
		const struct wmr_distortion_3K *distortion3K = ec->distortion3K + i;

		/* The X coords start at 0 for the left eye, and display_size.x / 2.0 for the right */
		const struct xrt_vec2 pix_coords[4] = {
		    /* -eye_center_x, 0 */
		    {(1.0f * view) * (ec->display_size.x / 2.0f) - distortion3K->eye_center.x, 0.0f},
		    /* 0, -eye_center_y */
		    {0.0f, -distortion3K->eye_center.y},
		    /* width-eye_center_x, 0 */
		    {(1.0f + 1.0f * view) * (ec->display_size.x / 2.0f) - distortion3K->eye_center.x, 0.0f},
		    /* 0, height-eye_center_y */
		    {0.0f, ec->display_size.y - distortion3K->eye_center.y},
		};

		for (int c = 0; c < 4; c++) {
			const struct xrt_vec2 pix_coord = pix_coords[c];

			float k1 = distortion3K->k[0];
			float k2 = distortion3K->k[1];
			float k3 = distortion3K->k[2];

			float r2 = m_vec2_dot(pix_coord, pix_coord);

			/* distort the pixel */
			float d = 1.0f + r2 * (k1 + r2 * (k2 + r2 * k3));

			/* Map the distorted pixel coordinate back to normalised view plane coords using the inverse
			 * affine xform */
			struct xrt_vec3 p = {(pix_coord.x * d + distortion3K->eye_center.x),
			                     (pix_coord.y * d + distortion3K->eye_center.y), 1.0f};
			struct xrt_vec3 vp;

			math_matrix_3x3_transform_vec3(&distortion_params->inv_affine_xform, &p, &vp);
			vp.x /= vp.z;
			vp.y /= vp.z;

			if (pix_coord.x < 0.0f) {
				if (vp.x < tanangle_left)
					tanangle_left = vp.x;
			} else {
				if (vp.x > tanangle_right)
					tanangle_right = vp.x;
			}

			if (pix_coord.y < 0.0f) {
				if (vp.y < tanangle_up)
					tanangle_up = vp.y;
			} else {
				if (vp.y > tanangle_down)
					tanangle_down = vp.y;
			}

			WMR_DEBUG(wh, "channel %d delta coord %f, %f d pixel %f %f, %f -> %f, %f", i, pix_coord.x,
			          pix_coord.y, d, p.x, p.y, vp.x, vp.y);
		}
	}

	*out_angle_left = atanf(tanangle_left);
	*out_angle_right = atanf(tanangle_right);
	*out_angle_down = -atanf(tanangle_down);
	*out_angle_up = -atanf(tanangle_up);
}

/*!
 * Creates an OpenCV-compatible @ref t_stereo_camera_calibration pointer from
 * the WMR config.
 *
 * Note that the camera model used on WMR headsets seems to be the same as the
 * one in Azure-Kinect-Sensor-SDK. That model is slightly different than
 * OpenCV's in the following ways:
 * 1. There are "center of distortion", codx and cody, parameters
 * 2. The terms that use the tangential parameters, p1 and p2, aren't multiplied by 2
 * 3. There is a "metric radius" that delimits a valid area of distortion/undistortion
 *
 * Thankfully, parameters of points 1 and 2 tend to be almost zero in practice and we
 * only do unprojections (for hand tracking) in very safe camera regions so 3
 * doesn't bother us that much either.
 */
XRT_MAYBE_UNUSED static struct t_stereo_camera_calibration *
wmr_hmd_create_stereo_camera_calib(struct wmr_hmd *wh)
{
	struct t_stereo_camera_calibration *calib = NULL;
	t_stereo_camera_calibration_alloc(&calib, 8);

	// Intrinsics
	for (int view = 0; view < 2; view++) { // Assuming that cameras[0-1] are HT0 and HT1
		struct t_camera_calibration *tcc = &calib->view[view];
		struct wmr_camera_config *cam = &wh->config.cameras[view];
		struct wmr_distortion_6KT *intr = &cam->distortion6KT;

		tcc->image_size_pixels.h = wh->config.cameras[view].roi.extent.h;
		tcc->image_size_pixels.w = wh->config.cameras[view].roi.extent.w;
		tcc->intrinsics[0][0] = intr->params.fx * (double)cam->roi.extent.w;
		tcc->intrinsics[1][1] = intr->params.fy * (double)cam->roi.extent.h;
		tcc->intrinsics[0][2] = intr->params.cx * (double)cam->roi.extent.w;
		tcc->intrinsics[1][2] = intr->params.cy * (double)cam->roi.extent.h;
		tcc->intrinsics[2][2] = 1.0;

		tcc->distortion[0] = intr->params.k[0];
		tcc->distortion[1] = intr->params.k[1];
		tcc->distortion[2] = intr->params.p1;
		tcc->distortion[3] = intr->params.p2;
		tcc->distortion[4] = intr->params.k[2];
		tcc->distortion[5] = intr->params.k[3];
		tcc->distortion[6] = intr->params.k[4];
		tcc->distortion[7] = intr->params.k[5];
		tcc->use_fisheye = false;
	}

	// Extrinsics

	// Compute transform from HT1 to HT0 (HT0 space into HT1 space)
	struct wmr_camera_config *ht1 = &wh->config.cameras[1];
	calib->camera_translation[0] = ht1->translation.x;
	calib->camera_translation[1] = ht1->translation.y;
	calib->camera_translation[2] = ht1->translation.z;
	calib->camera_rotation[0][0] = ht1->rotation.v[0];
	calib->camera_rotation[0][1] = ht1->rotation.v[1];
	calib->camera_rotation[0][2] = ht1->rotation.v[2];
	calib->camera_rotation[1][0] = ht1->rotation.v[3];
	calib->camera_rotation[1][1] = ht1->rotation.v[4];
	calib->camera_rotation[1][2] = ht1->rotation.v[5];
	calib->camera_rotation[2][0] = ht1->rotation.v[6];
	calib->camera_rotation[2][1] = ht1->rotation.v[7];
	calib->camera_rotation[2][2] = ht1->rotation.v[8];

	return calib;
}

XRT_MAYBE_UNUSED static struct t_imu_calibration
wmr_hmd_create_imu_calib(struct wmr_hmd *wh)
{
	float *at = wh->config.sensors.accel.mix_matrix.v;
	struct xrt_vec3 ao = wh->config.sensors.accel.bias_offsets;
	struct xrt_vec3 ab = wh->config.sensors.accel.bias_var;
	struct xrt_vec3 an = wh->config.sensors.accel.noise_std;

	float *gt = wh->config.sensors.gyro.mix_matrix.v;
	struct xrt_vec3 go = wh->config.sensors.gyro.bias_offsets;
	struct xrt_vec3 gb = wh->config.sensors.gyro.bias_var;
	struct xrt_vec3 gn = wh->config.sensors.gyro.noise_std;

	struct t_imu_calibration calib = {
	    .accel =
	        {
	            .transform = {{at[0], at[1], at[2]}, {at[3], at[4], at[5]}, {at[6], at[7], at[8]}},
	            .offset = {-ao.x, -ao.y, -ao.z}, // negative because slam system will add, not subtract
	            .bias_std = {sqrt(ab.x), sqrt(ab.y), sqrt(ab.z)}, // sqrt because we want stdev not variance
	            .noise_std = {an.x, an.y, an.z},
	        },
	    .gyro =
	        {
	            .transform = {{gt[0], gt[1], gt[2]}, {gt[3], gt[4], gt[5]}, {gt[6], gt[7], gt[8]}},
	            .offset = {-go.x, -go.y, -go.z},
	            .bias_std = {sqrt(gb.x), sqrt(gb.y), sqrt(gb.z)},
	            .noise_std = {gn.x, gn.y, gn.z},
	        },
	};
	return calib;
}

//! IMU extrinsics, frequencies, and rpmax
XRT_MAYBE_UNUSED static struct t_slam_calib_extras
wmr_hmd_create_extra_calib(struct wmr_hmd *wh)
{
	struct wmr_camera_config *ht0 = &wh->config.cameras[0];
	struct wmr_camera_config *ht1 = &wh->config.cameras[1];

	struct xrt_pose P_imu_ht0 = wh->config.sensors.accel.pose;
	struct xrt_pose P_ht1_ht0 = ht1->pose;
	struct xrt_pose P_ht0_ht1;
	math_pose_invert(&P_ht1_ht0, &P_ht0_ht1);
	struct xrt_pose P_imu_ht1;
	math_pose_transform(&P_imu_ht0, &P_ht0_ht1, &P_imu_ht1);

	struct xrt_matrix_4x4 T_imu_ht0;
	struct xrt_matrix_4x4 T_imu_ht1;
	math_matrix_4x4_isometry_from_pose(&P_imu_ht0, &T_imu_ht0);
	math_matrix_4x4_isometry_from_pose(&P_imu_ht1, &T_imu_ht1);

	//! @note This might change during runtime but the calibration data will be already submitted
	double imu_frequency = wh->average_imus ? IMU_FREQUENCY / IMU_SAMPLES_PER_PACKET : IMU_FREQUENCY;

	struct t_slam_calib_extras calib = {
	    .imu_frequency = imu_frequency,
	    .cams =
	        {
	            {
	                .frequency = CAMERA_FREQUENCY,
	                .T_imu_cam = T_imu_ht0,
	                .rpmax = ht0->distortion6KT.params.metric_radius,
	            },
	            {
	                .frequency = CAMERA_FREQUENCY,
	                .T_imu_cam = T_imu_ht1,
	                .rpmax = ht1->distortion6KT.params.metric_radius,
	            },
	        },
	};
	return calib;
}

static void
wmr_hmd_switch_hmd_tracker(void *wh_ptr)
{
	DRV_TRACE_MARKER();

	struct wmr_hmd *wh = (struct wmr_hmd *)wh_ptr;
	wh->slam_over_3dof = !wh->slam_over_3dof;
	struct u_var_button *btn = &wh->gui.switch_tracker_btn;

	if (wh->slam_over_3dof) { // Use SLAM
		snprintf(btn->label, sizeof(btn->label), "Switch to 3DoF Tracking");
	} else { // Use 3DoF
		snprintf(btn->label, sizeof(btn->label), "Switch to SLAM Tracking");
		os_mutex_lock(&wh->fusion.mutex);
		m_imu_3dof_reset(&wh->fusion.i3dof);
		wh->fusion.i3dof.rot = wh->pose.orientation;
		os_mutex_unlock(&wh->fusion.mutex);
	}
}

static struct xrt_slam_sinks *
wmr_hmd_slam_track(struct wmr_hmd *wh,
                   struct t_stereo_camera_calibration *stereo_calib,
                   struct t_imu_calibration *imu_calib,
                   struct t_slam_calib_extras *extra_calib)
{
	DRV_TRACE_MARKER();

	struct xrt_slam_sinks *sinks = NULL;

#ifdef XRT_FEATURE_SLAM
	struct t_slam_tracker_config config = {0};
	t_slam_fill_default_config(&config);
	config.stereo_calib = stereo_calib; // No need to do refcount here
	config.imu_calib = imu_calib;
	config.extra_calib = extra_calib;
	if (debug_get_option_slam_submit_from_start() == NULL) {
		config.submit_from_start = true;
	}

	int create_status = t_slam_create(&wh->tracking.xfctx, &config, &wh->tracking.slam, &sinks);
	if (create_status != 0) {
		return NULL;
	}

	int start_status = t_slam_start(wh->tracking.slam);
	if (start_status != 0) {
		return NULL;
	}

	WMR_DEBUG(wh, "WMR HMD SLAM tracker successfully started");
#endif

	return sinks;
}

static int
wmr_hmd_hand_track(struct wmr_hmd *wh,
                   struct t_stereo_camera_calibration *stereo_calib,
                   struct xrt_slam_sinks **out_sinks,
                   struct xrt_device **out_device)
{
	DRV_TRACE_MARKER();

	struct xrt_slam_sinks *sinks = NULL;
	struct xrt_device *device = NULL;

#ifdef XRT_BUILD_DRIVER_HANDTRACKING
	//!@todo Turning it off is okay for now, but we should plug metric_radius (or whatever it's called) in, at some
	//! point.
	struct t_camera_extra_info extra_camera_info = {0};

	extra_camera_info.views[0].camera_orientation = CAMERA_ORIENTATION_0;
	extra_camera_info.views[1].camera_orientation = CAMERA_ORIENTATION_0;

	extra_camera_info.views[0].boundary_type = HT_IMAGE_BOUNDARY_NONE;
	extra_camera_info.views[1].boundary_type = HT_IMAGE_BOUNDARY_NONE;

	int create_status = ht_device_create(&wh->tracking.xfctx,  //
	                                     stereo_calib,         //
	                                     HT_ALGORITHM_MERCURY, //
	                                     extra_camera_info,    //
	                                     &sinks,               //
	                                     &device);
	if (create_status != 0) {
		return create_status;
	}

	device = multi_create_tracking_override(XRT_TRACKING_OVERRIDE_ATTACHED, device, &wh->base,
	                                        XRT_INPUT_GENERIC_HEAD_POSE, &wh->P_ht0_me);

	WMR_DEBUG(wh, "WMR HMD hand tracker successfully created");
#endif

	*out_sinks = sinks;
	*out_device = device;

	return 0;
}

static void
wmr_hmd_setup_ui(struct wmr_hmd *wh)
{
	u_var_add_root(wh, "WMR HMD", true);

	u_var_add_gui_header(wh, NULL, "Tracking");
	if (wh->tracking.slam_enabled) {
		wh->gui.switch_tracker_btn.cb = wmr_hmd_switch_hmd_tracker;
		wh->gui.switch_tracker_btn.ptr = wh;
		u_var_add_button(wh, &wh->gui.switch_tracker_btn, "Switch to 3DoF Tracking");
	}
	u_var_add_pose(wh, &wh->pose, "Tracked Pose");
	u_var_add_pose(wh, &wh->offset, "Pose Offset");
	u_var_add_bool(wh, &wh->average_imus, "Average IMU samples");

	u_var_add_gui_header(wh, NULL, "3DoF Tracking");
	m_imu_3dof_add_vars(&wh->fusion.i3dof, wh, "");

	u_var_add_gui_header(wh, NULL, "SLAM Tracking");
	u_var_add_ro_text(wh, wh->gui.slam_status, "Tracker status");
	u_var_add_bool(wh, &wh->tracking.imu2me, "Correct IMU pose to middle of eyes");

	u_var_add_gui_header(wh, NULL, "Hand Tracking");
	u_var_add_ro_text(wh, wh->gui.hand_status, "Tracker status");

	u_var_add_gui_header(wh, NULL, "Hololens Sensors' Companion device");
	u_var_add_u8(wh, &wh->proximity_sensor, "HMD Proximity");
	u_var_add_u16(wh, &wh->raw_ipd, "HMD IPD");

	if (wh->hmd_desc->screen_enable_func) {
		// Enabling/disabling the HMD screen at runtime is supported. Add button to debug GUI.
		wh->gui.hmd_screen_enable_btn.cb = wmr_hmd_screen_enable_toggle;
		wh->gui.hmd_screen_enable_btn.ptr = wh;
		u_var_add_button(wh, &wh->gui.hmd_screen_enable_btn, "HMD Screen [On/Off]");
	}

	u_var_add_gui_header(wh, NULL, "Misc");
	u_var_add_log_level(wh, &wh->log_level, "log_level");
}

/*!
 * Procedure to setup trackers: 3dof, SLAM and hand tracking.
 *
 * Determines which trackers to initialize and starts them.
 * Fills @p out_sinks to stream raw data to for tracking.
 * In the case of hand tracking being enabled, it returns a hand tracker device in @p out_handtracker.
 *
 * @param wh the wmr headset device
 * @param out_sinks sinks to stream video/IMU data to for tracking
 * @param out_handtracker a newly created hand tracker device
 * @return true on success, false when an unexpected state is reached.
 */
static bool
wmr_hmd_setup_trackers(struct wmr_hmd *wh, struct xrt_slam_sinks *out_sinks, struct xrt_device **out_handtracker)
{
	// We always have at least 3dof HMD tracking
	bool dof3_enabled = true;

	// Decide whether to initialize the SLAM tracker
	bool slam_wanted = debug_get_bool_option_wmr_slam();
#ifdef XRT_FEATURE_SLAM
	bool slam_supported = true;
#else
	bool slam_supported = false;
#endif
	bool slam_enabled = slam_supported && slam_wanted;

	// Decide whether to initialize the hand tracker
	bool hand_wanted = debug_get_bool_option_wmr_handtracking();
#ifdef XRT_BUILD_DRIVER_HANDTRACKING
	bool hand_supported = true;
#else
	bool hand_supported = false;
#endif
	bool hand_enabled = hand_supported && hand_wanted;

	wh->base.orientation_tracking_supported = dof3_enabled || slam_enabled;
	wh->base.position_tracking_supported = slam_enabled;
	wh->base.hand_tracking_supported = false; // out_handtracker will handle it

	wh->tracking.slam_enabled = slam_enabled;
	wh->tracking.hand_enabled = hand_enabled;
	wh->tracking.imu2me = true;

	wh->slam_over_3dof = slam_enabled; // We prefer SLAM over 3dof tracking if possible

	const char *slam_status = wh->tracking.slam_enabled ? "Enabled"
	                          : !slam_wanted            ? "Disabled by the user (envvar set to false)"
	                          : !slam_supported         ? "Unavailable (not built)"
	                                                    : NULL;

	const char *hand_status = wh->tracking.hand_enabled ? "Enabled"
	                          : !hand_wanted            ? "Disabled by the user (envvar set to false)"
	                          : !hand_supported         ? "Unavailable (not built)"
	                                                    : NULL;

	assert(slam_status != NULL && hand_status != NULL);

	snprintf(wh->gui.slam_status, sizeof(wh->gui.slam_status), "%s", slam_status);
	snprintf(wh->gui.hand_status, sizeof(wh->gui.hand_status), "%s", hand_status);

	struct t_stereo_camera_calibration *stereo_calib = wmr_hmd_create_stereo_camera_calib(wh);
	struct t_imu_calibration imu_calib = wmr_hmd_create_imu_calib(wh);
	struct t_slam_calib_extras extra_calib = wmr_hmd_create_extra_calib(wh);

	// Initialize 3DoF tracker
	m_imu_3dof_init(&wh->fusion.i3dof, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	// Initialize SLAM tracker
	struct xrt_slam_sinks *slam_sinks = NULL;
	if (wh->tracking.slam_enabled) {
		slam_sinks = wmr_hmd_slam_track(wh, stereo_calib, &imu_calib, &extra_calib);
		if (slam_sinks == NULL) {
			WMR_WARN(wh, "Unable to setup the SLAM tracker");
			return false;
		}
	}

	// Initialize hand tracker
	struct xrt_slam_sinks *hand_sinks = NULL;
	struct xrt_device *hand_device = NULL;
	if (wh->tracking.hand_enabled) {
		int hand_status = wmr_hmd_hand_track(wh, stereo_calib, &hand_sinks, &hand_device);
		if (hand_status != 0 || hand_sinks == NULL || hand_device == NULL) {
			WMR_WARN(wh, "Unable to setup the hand tracker");
			return false;
		}
	}

	t_stereo_camera_calibration_reference(&stereo_calib, NULL);

	// Setup sinks depending on tracking configuration
	struct xrt_slam_sinks entry_sinks = {0};
	if (slam_enabled && hand_enabled) {
		struct xrt_frame_sink *entry_left_sink = NULL;
		struct xrt_frame_sink *entry_right_sink = NULL;

		u_sink_split_create(&wh->tracking.xfctx, slam_sinks->left, hand_sinks->left, &entry_left_sink);
		u_sink_split_create(&wh->tracking.xfctx, slam_sinks->right, hand_sinks->right, &entry_right_sink);

		entry_sinks = (struct xrt_slam_sinks){
		    .left = entry_left_sink,
		    .right = entry_right_sink,
		    .imu = slam_sinks->imu,
		    .gt = slam_sinks->gt,
		};
	} else if (slam_enabled) {
		entry_sinks = *slam_sinks;
	} else if (hand_enabled) {
		entry_sinks = *hand_sinks;
	} else {
		entry_sinks = (struct xrt_slam_sinks){0};
	}

	*out_sinks = entry_sinks;
	*out_handtracker = hand_device;
	return true;
}

/*!
 * Precompute transforms to convert between OpenXR and WMR coordinate systems.
 *
 * OpenXR: X: Right, Y: Up, Z: Backward
 * WMR: X: Right, Y: Down, Z: Forward
 * ┌────────────────────┐
 * │   OXR       WMR    │
 * │                    │
 * │ ▲ y                │
 * │ │         ▲ z      │
 * │ │    x    │    x   │
 * │ ├──────►  ├──────► │
 * │ │         │        │
 * │ ▼ z       │        │
 * │           ▼ y      │
 * └────────────────────┘
 */
static void
precompute_sensor_transforms(struct wmr_hmd *wh)
{
	// P_A_B is such that B = P_A_B * A. See conventions.md
	struct xrt_pose P_oxr_wmr = {{.x = 1.0, .y = 0.0, .z = 0.0, .w = 0.0}, XRT_VEC3_ZERO};
	struct xrt_pose P_wmr_oxr = {0};
	struct xrt_pose P_acc_ht0 = wh->config.sensors.accel.pose;
	struct xrt_pose P_gyr_ht0 = wh->config.sensors.gyro.pose;
	struct xrt_pose P_ht0_acc = {0};
	struct xrt_pose P_ht0_gyr = {0};
	struct xrt_pose P_me_ht0 = {0}; // "me" == "middle of the eyes"
	struct xrt_pose P_me_acc = {0};
	struct xrt_pose P_me_gyr = {0};
	struct xrt_pose P_ht0_me = {0};
	struct xrt_pose P_acc_me = {0};
	struct xrt_pose P_oxr_ht0_me = {0}; // P_ht0_me in OpenXR coordinates
	struct xrt_pose P_oxr_acc_me = {0}; // P_acc_me in OpenXR coordinates

	// All of the observed headsets have reported a zero translation for its gyro
	assert(m_vec3_equal_exact(P_gyr_ht0.position, (struct xrt_vec3){0, 0, 0}));

	// Initialize transforms

	// All of these are in WMR coordinates.
	math_pose_invert(&P_oxr_wmr, &P_wmr_oxr); // P_wmr_oxr == P_oxr_wmr
	math_pose_invert(&P_acc_ht0, &P_ht0_acc);
	math_pose_invert(&P_gyr_ht0, &P_ht0_gyr);
	math_pose_interpolate(&wh->config.eye_params[0].pose, &wh->config.eye_params[1].pose, 0.5, &P_me_ht0);
	math_pose_transform(&P_me_ht0, &P_ht0_acc, &P_me_acc);
	math_pose_transform(&P_me_ht0, &P_ht0_gyr, &P_me_gyr);
	math_pose_invert(&P_me_ht0, &P_ht0_me);
	math_pose_invert(&P_me_acc, &P_acc_me);

	// Express P_*_me pose in OpenXR coordinates through sandwich products.
	math_pose_transform(&P_acc_me, &P_wmr_oxr, &P_oxr_acc_me);
	math_pose_transform(&P_oxr_wmr, &P_oxr_acc_me, &P_oxr_acc_me);
	math_pose_transform(&P_ht0_me, &P_wmr_oxr, &P_oxr_ht0_me);
	math_pose_transform(&P_oxr_wmr, &P_oxr_ht0_me, &P_oxr_ht0_me);

	// Save transforms
	math_pose_transform(&P_oxr_wmr, &P_me_acc, &wh->P_oxr_acc);
	math_pose_transform(&P_oxr_wmr, &P_me_gyr, &wh->P_oxr_gyr);
	wh->P_ht0_me = P_oxr_ht0_me;
	wh->P_imu_me = P_oxr_acc_me; // Assume accel pose is IMU pose
}

void
wmr_hmd_create(enum wmr_headset_type hmd_type,
               struct os_hid_device *hid_holo,
               struct os_hid_device *hid_ctrl,
               struct xrt_prober_device *dev_holo,
               enum u_logging_level log_level,
               struct xrt_device **out_hmd,
               struct xrt_device **out_handtracker)
{
	DRV_TRACE_MARKER();

	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	int ret = 0;
	int i;
	int eye;

	struct wmr_hmd *wh = U_DEVICE_ALLOCATE(struct wmr_hmd, flags, 1, 0);
	if (!wh) {
		return;
	}

	// Populate the base members.
	wh->base.update_inputs = wmr_hmd_update_inputs;
	wh->base.get_tracked_pose = wmr_hmd_get_tracked_pose;
	wh->base.get_view_poses = wmr_hmd_get_view_poses;
	wh->base.destroy = wmr_hmd_destroy;
	wh->base.name = XRT_DEVICE_GENERIC_HMD;
	wh->base.device_type = XRT_DEVICE_TYPE_HMD;
	wh->log_level = log_level;

	wh->hid_hololens_sensors_dev = hid_holo;
	wh->hid_control_dev = hid_ctrl;

	// Mutex before thread.
	ret = os_mutex_init(&wh->fusion.mutex);
	if (ret != 0) {
		WMR_ERROR(wh, "Failed to init mutex!");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return;
	}

	// Thread and other state.
	ret = os_thread_helper_init(&wh->oth);
	if (ret != 0) {
		WMR_ERROR(wh, "Failed to init threading!");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return;
	}

	// Setup input.
	wh->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	// Read config file from HMD
	if (wmr_read_config(wh) < 0) {
		WMR_ERROR(wh, "Failed to load headset configuration!");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return;
	}

	wh->pose = (struct xrt_pose)XRT_POSE_IDENTITY;
	wh->offset = (struct xrt_pose)XRT_POSE_IDENTITY;
	wh->average_imus = true;

	/* Now that we have the config loaded, iterate the map of known headsets and see if we have
	 * an entry for this specific headset (otherwise the generic entry will be used)
	 */
	for (i = 0; i < headset_map_n; i++) {
		const struct wmr_headset_descriptor *cur = &headset_map[i];

		if (hmd_type == cur->hmd_type) {
			wh->hmd_desc = cur;
			if (hmd_type != WMR_HEADSET_GENERIC)
				break; /* Stop checking if we have a specific match, or keep going for the GENERIC
				          catch-all type */
		}

		if (cur->dev_id_str && strncmp(wh->config_hdr.name, cur->dev_id_str, 64) == 0) {
			hmd_type = cur->hmd_type;
			wh->hmd_desc = cur;
			break;
		}
	}
	assert(wh->hmd_desc != NULL); /* Each supported device MUST have a manually created entry in our headset_map */

	WMR_INFO(wh, "Found WMR headset type: %s", wh->hmd_desc->debug_name);

	precompute_sensor_transforms(wh);

	struct u_extents_2d exts;
	exts.w_pixels = (uint32_t)wh->config.eye_params[0].display_size.x;
	exts.h_pixels = (uint32_t)wh->config.eye_params[0].display_size.y;
	u_extents_2d_split_side_by_side(&wh->base, &exts);

	// Fill in blend mode - just opqaue, unless we get Hololens support one day.
	size_t idx = 0;
	wh->base.hmd->blend_modes[idx++] = XRT_BLEND_MODE_OPAQUE;
	wh->base.hmd->blend_mode_count = idx;

	// Distortion information, fills in xdev->compute_distortion().
	for (eye = 0; eye < 2; eye++) {
		math_matrix_3x3_inverse(&wh->config.eye_params[eye].affine_xform,
		                        &wh->distortion_params[eye].inv_affine_xform);

		compute_distortion_bounds(wh, eye, &wh->base.hmd->distortion.fov[eye].angle_left,
		                          &wh->base.hmd->distortion.fov[eye].angle_right,
		                          &wh->base.hmd->distortion.fov[eye].angle_down,
		                          &wh->base.hmd->distortion.fov[eye].angle_up);

		WMR_INFO(wh, "FoV eye %d angles left %f right %f down %f up %f", eye,
		         wh->base.hmd->distortion.fov[eye].angle_left, wh->base.hmd->distortion.fov[eye].angle_right,
		         wh->base.hmd->distortion.fov[eye].angle_down, wh->base.hmd->distortion.fov[eye].angle_up);

		wh->distortion_params[eye].tex_x_range.x = tanf(wh->base.hmd->distortion.fov[eye].angle_left);
		wh->distortion_params[eye].tex_x_range.y = tanf(wh->base.hmd->distortion.fov[eye].angle_right);
		wh->distortion_params[eye].tex_y_range.x = tanf(wh->base.hmd->distortion.fov[eye].angle_down);
		wh->distortion_params[eye].tex_y_range.y = tanf(wh->base.hmd->distortion.fov[eye].angle_up);

		WMR_INFO(wh, "Render texture range %f, %f to %f, %f", wh->distortion_params[eye].tex_x_range.x,
		         wh->distortion_params[eye].tex_y_range.x, wh->distortion_params[eye].tex_x_range.y,
		         wh->distortion_params[eye].tex_y_range.y);
	}

	wh->base.hmd->distortion.models = XRT_DISTORTION_MODEL_COMPUTE;
	wh->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_COMPUTE;
	wh->base.compute_distortion = compute_distortion_wmr;
	u_distortion_mesh_fill_in_compute(&wh->base);

	// Set initial HMD screen power state.
	wh->hmd_screen_enable = true;

	/* We're set up. Activate the HMD and turn on the IMU */
	if (wh->hmd_desc->init_func && wh->hmd_desc->init_func(wh) != 0) {
		WMR_ERROR(wh, "Activation of HMD failed");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return;
	}

	// Switch on IMU on the HMD.
	hololens_sensors_enable_imu(wh);

	// Switch on data streams on the HMD (only cameras for now as IMU is not yet integrated into wmr_source)
	wh->tracking.source = wmr_source_create(&wh->tracking.xfctx, dev_holo, wh->config);

	struct xrt_slam_sinks sinks = {0};
	struct xrt_device *hand_device = NULL;
	bool success = wmr_hmd_setup_trackers(wh, &sinks, &hand_device);
	if (!success) {
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return;
	}

	// Stream data source into sinks (if populated)
	bool stream_started = xrt_fs_slam_stream_start(wh->tracking.source, &sinks);
	if (!stream_started) {
		//! @todo Could reach this due to !XRT_HAVE_LIBUSB but the HMD should keep working
		WMR_WARN(wh, "Failed to start WMR source");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return;
	}

	// Hand over hololens sensor device to reading thread.
	ret = os_thread_helper_start(&wh->oth, wmr_run_thread, wh);
	if (ret != 0) {
		WMR_ERROR(wh, "Failed to start thread!");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return;
	}

	wmr_hmd_setup_ui(wh);

	*out_hmd = &wh->base;
	*out_handtracker = hand_device;
}
