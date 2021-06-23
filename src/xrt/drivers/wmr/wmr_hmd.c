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
#include "math/m_predict.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_distortion_mesh.h"

#include "wmr_hmd.h"
#include "wmr_common.h"
#include "wmr_config_key.h"
#include "wmr_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifndef XRT_OS_WINDOWS
#include <unistd.h> // for sleep()
#endif

static int
wmr_hmd_activate_reverb(struct wmr_hmd *wh);
static void
wmr_hmd_deactivate_reverb(struct wmr_hmd *wh);

const struct wmr_headset_descriptor headset_map[] = {
    {WMR_HEADSET_GENERIC, NULL, "Unknown WMR HMD", NULL, NULL}, /* Catch-all for unknown headsets */
    {WMR_HEADSET_REVERB_G1, "HP Reverb VR Headset VR1000-2xxx", "HP Reverb", wmr_hmd_activate_reverb,
     wmr_hmd_deactivate_reverb},
    {WMR_HEADSET_REVERB_G2, "HP Reverb Virtual Reality Headset G2", "HP Reverb G2", wmr_hmd_activate_reverb,
     wmr_hmd_deactivate_reverb},
    {WMR_HEADSET_SAMSUNG_800ZAA, "Samsung Windows Mixed Reality 800ZAA", "Samsung Odyssey", NULL, NULL},
    {WMR_HEADSET_LENOVO_EXPLORER, "Lenovo VR-2511N", "Lenovo Explorer", NULL, NULL},
};
const int headset_map_n = sizeof(headset_map) / sizeof(headset_map[0]);

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

	return;
}

static bool
hololens_sensors_read_packets(struct wmr_hmd *wh)
{
	WMR_TRACE(wh, " ");

	unsigned char buffer[WMR_FEATURE_BUFFER_SIZE];

	// Block for 100ms
	int size = os_hid_read(wh->hid_hololens_sensors_dev, buffer, sizeof(buffer), 100);

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
	case WMR_MS_HOLOLENS_MSG_SENSORS: {
		// Get the timing as close to reading the packet as possible.
		uint64_t now_ns = os_monotonic_get_ns();

		hololens_sensors_decode_packet(wh, &wh->packet, buffer, size);

		struct xrt_vec3 raw_gyro[4];
		struct xrt_vec3 raw_accel[4];

		for (int i = 0; i < 4; i++) {
			struct xrt_vec3 sample;
			vec3_from_hololens_gyro(wh->packet.gyro, i, &sample);
			math_quat_rotate_vec3(&wh->gyro_to_centerline.orientation, &sample, &raw_gyro[i]);

			vec3_from_hololens_accel(wh->packet.accel, i, &sample);
			math_quat_rotate_vec3(&wh->accel_to_centerline.orientation, &sample, &raw_accel[i]);
		}

		os_mutex_lock(&wh->fusion.mutex);
		for (int i = 0; i < 4; i++) {
			m_imu_3dof_update(                                              //
			    &wh->fusion.i3dof,                                          //
			    wh->packet.gyro_timestamp[i] * WMR_MS_HOLOLENS_NS_PER_TICK, //
			    &raw_accel[i],                                              //
			    &raw_gyro[i]);                                              //
		}
		wh->fusion.last_imu_timestamp_ns = now_ns;
		wh->fusion.last_angular_velocity = raw_gyro[3];
		os_mutex_unlock(&wh->fusion.mutex);

		break;
	}
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
wmr_hmd_deactivate_reverb(struct wmr_hmd *wh)
{
	struct os_hid_device *hid = wh->hid_control_dev;

	/* Turn the screen off */
	unsigned char cmd[2] = {0x04, 0x00};
	HID_SEND(hid, cmd, "screen_off");
}


/*
 *
 * Config functions.
 *
 */

static int
wmr_config_command_sync(struct wmr_hmd *wh, unsigned char type, unsigned char *buf, int len)
{
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
	} while (buf[0] == WMR_MS_HOLOLENS_MSG_SENSORS || //
	         buf[0] == WMR_MS_HOLOLENS_MSG_DEBUG ||   //
	         buf[0] == WMR_MS_HOLOLENS_MSG_UNKNOWN_17);

	return -1;
}

static int
wmr_read_config_part(struct wmr_hmd *wh, unsigned char type, unsigned char *data, int len)
{

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
	unsigned char meta[84];
	uint8_t *data;
	int size, data_size;

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
	unsigned char *data = NULL, *config_json_block;
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

	if (!wmr_config_parse(&wh->config, (char *)config_json_block, wh->log_level)) {
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

	// Variables needed for prediction.
	uint64_t last_imu_timestamp_ns = 0;
	struct xrt_space_relation relation = {0};
	relation.relation_flags = (enum xrt_space_relation_flags)( //
	    XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT |        //
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |             //
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

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
}

static void
wmr_hmd_get_view_pose(struct xrt_device *xdev,
                      const struct xrt_vec3 *eye_relation,
                      uint32_t view_index,
                      struct xrt_pose *out_pose)
{
	(void)xdev;
	u_device_get_view_pose(eye_relation, view_index, out_pose);
}

static void
wmr_hmd_destroy(struct xrt_device *xdev)
{
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

	// Destroy the fusion.
	m_imu_3dof_close(&wh->fusion.i3dof);

	os_mutex_destroy(&wh->fusion.mutex);

	free(wh);
}

static bool
compute_distortion_wmr(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
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
		struct xrt_vec2 pix_coord = {(u + 1.0 * view) * (ec->display_size.x / 2.0) - distortion3K->eye_center.x,
		                             v * ec->display_size.y - distortion3K->eye_center.y};

		float r2 = m_vec2_dot(pix_coord, pix_coord);
		float k1 = distortion3K->k[0];
		float k2 = distortion3K->k[1];
		float k3 = distortion3K->k[2];

		float d = 1.0 + r2 * (k1 + r2 * (k2 + r2 * k3));

		/* Map the distorted pixel coordinate back to normalised view plane coords using the inverse affine
		 * xform */
		struct xrt_vec3 p = {(pix_coord.x * d + distortion3K->eye_center.x),
		                     (pix_coord.y * d + distortion3K->eye_center.y), 1.0};
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

struct xrt_device *
wmr_hmd_create(enum wmr_headset_type hmd_type,
               struct os_hid_device *hid_holo,
               struct os_hid_device *hid_ctrl,
               enum u_logging_level ll)
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	int ret = 0, i;
	int eye;

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
	wh->hid_hololens_sensors_dev = hid_holo;
	wh->hid_control_dev = hid_ctrl;

	// Mutex before thread.
	ret = os_mutex_init(&wh->fusion.mutex);
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

	// Setup input.
	wh->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	// Read config file from HMD
	if (wmr_read_config(wh) < 0) {
		WMR_ERROR(wh, "Failed to load headset configuration!");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return NULL;
	}

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
	assert(wh->hmd_desc != NULL); /* We must have matched something, or the map is set up wrong */

	WMR_INFO(wh, "Found WMR headset type: %s", wh->hmd_desc->debug_name);

	// Compute centerline in the HMD's calibration coordinate space as the average of the two display poses
	math_quat_slerp(&wh->config.eye_params[0].pose.orientation, &wh->config.eye_params[1].pose.orientation, 0.5f,
	                &wh->centerline.orientation);
	wh->centerline.position.x =
	    (wh->config.eye_params[0].pose.position.x + wh->config.eye_params[1].pose.position.x) * 0.5f;
	wh->centerline.position.y =
	    (wh->config.eye_params[0].pose.position.y + wh->config.eye_params[1].pose.position.y) * 0.5f;
	wh->centerline.position.z =
	    (wh->config.eye_params[0].pose.position.z + wh->config.eye_params[1].pose.position.z) * 0.5f;

	// Compute display and sensor offsets relative to the centerline
	for (int dIdx = 0; dIdx < 2; ++dIdx) {
		math_pose_invert(&wh->config.eye_params[dIdx].pose, &wh->display_to_centerline[dIdx]);
		math_pose_transform(&wh->centerline, &wh->display_to_centerline[dIdx],
		                    &wh->display_to_centerline[dIdx]);
	}
	math_pose_invert(&wh->config.accel_pose, &wh->accel_to_centerline);
	math_pose_transform(&wh->centerline, &wh->accel_to_centerline, &wh->accel_to_centerline);
	math_pose_invert(&wh->config.gyro_pose, &wh->gyro_to_centerline);
	math_pose_transform(&wh->centerline, &wh->gyro_to_centerline, &wh->gyro_to_centerline);
	math_pose_invert(&wh->config.mag_pose, &wh->mag_to_centerline);
	math_pose_transform(&wh->centerline, &wh->mag_to_centerline, &wh->mag_to_centerline);

	struct u_device_simple_info info;
	info.display.w_pixels = wh->config.eye_params[0].display_size.x;
	info.display.h_pixels = wh->config.eye_params[0].display_size.y;

	info.lens_horizontal_separation_meters =
	    fabs(wh->display_to_centerline[1].position.x - wh->display_to_centerline[0].position.x);

	// TODO placeholder values below here
	info.display.w_meters = 0.13f;
	info.display.h_meters = 0.07f;
	info.lens_vertical_position_meters = 0.07f / 2.0f;
	info.views[0].fov = 85.0f * (M_PI / 180.0f);
	info.views[1].fov = 85.0f * (M_PI / 180.0f);

	if (!u_device_setup_split_side_by_side(&wh->base, &info)) {
		WMR_ERROR(wh, "Failed to setup basic HMD device info");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return NULL;
	}

	m_imu_3dof_init(&wh->fusion.i3dof, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	// Setup variable tracker.
	u_var_add_root(wh, "WMR HMD", true);
	u_var_add_gui_header(wh, &wh->gui.fusion, "3DoF Fusion");
	m_imu_3dof_add_vars(&wh->fusion.i3dof, wh, "");
	u_var_add_gui_header(wh, &wh->gui.misc, "Misc");
	u_var_add_log_level(wh, &wh->log_level, "log_level");

	// Distortion information, fills in xdev->compute_distortion().
	for (eye = 0; eye < 2; eye++) {
		math_matrix_3x3_inverse(&wh->config.eye_params[eye].affine_xform,
		                        &wh->distortion_params[eye].inv_affine_xform);
		wh->distortion_params[eye].tex_x_range.x = tan(wh->base.hmd->views[eye].fov.angle_left);
		wh->distortion_params[eye].tex_x_range.y = tan(wh->base.hmd->views[eye].fov.angle_right);
		wh->distortion_params[eye].tex_y_range.x = tan(wh->base.hmd->views[eye].fov.angle_down);
		wh->distortion_params[eye].tex_y_range.y = tan(wh->base.hmd->views[eye].fov.angle_up);
	}

	wh->base.hmd->distortion.models = XRT_DISTORTION_MODEL_COMPUTE;
	wh->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_COMPUTE;
	wh->base.compute_distortion = compute_distortion_wmr;
	u_distortion_mesh_fill_in_compute(&wh->base);

	/* We're set up. Activate the HMD and turn on the IMU */
	if (wh->hmd_desc->init_func && wh->hmd_desc->init_func(wh) != 0) {
		WMR_ERROR(wh, "Activation of HMD failed");
		wmr_hmd_destroy(&wh->base);
		wh = NULL;
		return NULL;
	}

	// Switch on IMU on the HMD.
	hololens_sensors_enable_imu(wh);


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
