/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 */

/*!
 * @file
 * @brief  Oculus Rift S headset tracking system
 *
 * The Rift S system provides the HID/USB polling thread
 * and dispatches incoming packets to the HMD and controller
 * implementations.
 *
 * Ported from OpenHMD
 *
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */

/* Oculus Rift S Driver - HID/USB Driver Implementation */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <inttypes.h>

#include "math/m_api.h"
#include "math/m_vec3.h"

#include "os/os_time.h"

#include "util/u_device.h"
#include "util/u_distortion_mesh.h"
#include "util/u_trace_marker.h"
#include "util/u_var.h"

#include "xrt/xrt_device.h"

#include "rift_s.h"
#include "rift_s_hmd.h"
#include "rift_s_controller.h"
#include "rift_s_camera.h"

static void *
rift_s_run_thread(void *ptr);
static void
rift_s_system_free(struct rift_s_system *sys);

static int
read_camera_calibration(struct os_hid_device *hid_hmd, struct rift_s_camera_calibration_block *calibration)
{
	char *json = NULL;
	int json_len = 0;

	int ret = rift_s_read_firmware_block(hid_hmd, RIFT_S_FIRMWARE_BLOCK_CAMERA_CALIB, &json, &json_len);
	if (ret < 0)
		return ret;

	ret = rift_s_parse_camera_calibration_block(json, calibration);
	free(json);

	return ret;
}

static int
read_hmd_fw_imu_calibration(struct os_hid_device *hid_hmd, struct rift_s_imu_calibration *imu_calibration)
{
	char *json = NULL;
	int json_len = 0;

	int ret = rift_s_read_firmware_block(hid_hmd, RIFT_S_FIRMWARE_BLOCK_IMU_CALIB, &json, &json_len);
	if (ret < 0)
		return ret;

	ret = rift_s_parse_imu_calibration(json, imu_calibration);
	free(json);

	return ret;
}

static int
read_hmd_proximity_threshold(struct os_hid_device *hid_hmd, int *proximity_threshold)
{
	char *json = NULL;
	int json_len = 0;

	int ret = rift_s_read_firmware_block(hid_hmd, RIFT_S_FIRMWARE_BLOCK_THRESHOLD, &json, &json_len);
	if (ret < 0)
		return ret;

	ret = rift_s_parse_proximity_threshold(json, proximity_threshold);
	free(json);

	return ret;
}

static int
read_hmd_config(struct os_hid_device *hid_hmd, struct rift_s_hmd_config *config)
{
	int ret;

	ret = rift_s_read_firmware_version(hid_hmd);
	if (ret < 0) {
		RIFT_S_ERROR("Failed to read Rift S firmware version");
		return ret;
	}

	ret = rift_s_read_panel_info(hid_hmd, &config->panel_info);
	if (ret < 0) {
		RIFT_S_ERROR("Failed to read Rift S device info");
		return ret;
	}

	ret = rift_s_read_imu_config_info(hid_hmd, &config->imu_config_info);
	if (ret < 0) {
		RIFT_S_ERROR("Failed to read IMU configuration block");
		return ret;
	}

	ret = read_hmd_fw_imu_calibration(hid_hmd, &config->imu_calibration);
	if (ret < 0) {
		RIFT_S_ERROR("Failed to read IMU configuration block");
		return ret;
	}

	/* Configure the proximity sensor threshold */
	ret = read_hmd_proximity_threshold(hid_hmd, &config->proximity_threshold);
	if (ret < 0) {
		RIFT_S_ERROR("Failed to read proximity sensor firmware block");
		return ret;
	}

	ret = read_camera_calibration(hid_hmd, &config->camera_calibration);
	if (ret < 0) {
		RIFT_S_ERROR("Failed to read HMD camera calibration block");
		return ret;
	}

	return 0;
}

struct rift_s_system *
rift_s_system_create(struct xrt_prober *xp,
                     const unsigned char *hmd_serial_no,
                     struct os_hid_device *hid_hmd,
                     struct os_hid_device *hid_status,
                     struct os_hid_device *hid_controllers)
{
	int ret;

	DRV_TRACE_MARKER();

	struct rift_s_system *sys = U_TYPED_CALLOC(struct rift_s_system);
	sys->base.type = XRT_TRACKING_TYPE_NONE;
	sys->base.offset.orientation.w = 1.0f;

	/* Init refcount */
	sys->ref.count = 1;

	sys->handles[HMD_HID] = hid_hmd;
	sys->handles[STATUS_HID] = hid_status;
	sys->handles[CONTROLLER_HID] = hid_controllers;

	ret = os_mutex_init(&sys->dev_mutex);
	if (ret != 0) {
		RIFT_S_ERROR("Failed to init device mutex");
		goto cleanup;
	}
	//
	// Thread and other state.
	ret = os_thread_helper_init(&sys->oth);
	if (ret != 0) {
		RIFT_S_ERROR("Failed to init packet processing thread");
		goto cleanup;
	}

	if (read_hmd_config(hid_hmd, &sys->hmd_config) < 0) {
		RIFT_S_ERROR("Failed to read HMD configuration");
		goto cleanup;
	}

	sys->tracker = rift_s_tracker_create(&sys->base, &sys->xfctx, &sys->hmd_config);
	if (sys->tracker == NULL) {
		RIFT_S_ERROR("Failed to init tracking");
		goto cleanup;
	}

	rift_s_radio_state_init(&sys->radio_state);

	/* Create the HMD now. Controllers are created in the
	 * rift_s_system_get_controller() call later */
	struct rift_s_hmd *hmd = rift_s_hmd_create(sys, hmd_serial_no, &sys->hmd_config);
	if (hmd == NULL) {
		RIFT_S_ERROR("Failed to create Oculus Rift S device.");
		goto cleanup;
	}

	sys->hmd = hmd;

	// Start the packet reading thread
	ret = os_thread_helper_start(&sys->oth, rift_s_run_thread, sys);
	if (ret != 0) {
		RIFT_S_ERROR("Failed to start packet processing thread");
		goto cleanup;
	}

	/* Turn on the headset and display connection */
	if (rift_s_hmd_enable(sys->handles[HMD_HID], true) < 0) {
		RIFT_S_ERROR("Failed to enable Rift S");
		goto cleanup;
	}

	// Allow time for enumeration of available displays by host system, so the compositor can select among them.
	RIFT_S_INFO(
	    "Sleeping until the HMD display is powered up so, the available displays "
	    "can be enumerated by the host system.");

	// Two seconds seems to be needed for the display connection to stabilise
	os_nanosleep((uint64_t)U_TIME_1S_IN_NS * 2);

	// Start the camera input
	struct rift_s_camera *cam =
	    rift_s_camera_create(xp, &sys->xfctx, (const char *)hmd_serial_no, sys->handles[HMD_HID], sys->tracker,
	                         &sys->hmd_config.camera_calibration);
	if (cam == NULL) {
		RIFT_S_ERROR("Failed to open Rift S camera device");
		goto cleanup;
	}
	os_mutex_lock(&sys->dev_mutex);
	sys->cam = cam;
	os_mutex_unlock(&sys->dev_mutex);

	rift_s_tracker_start(sys->tracker);

	RIFT_S_DEBUG("Oculus Rift S driver ready");

	return sys;

cleanup:
	if (sys->hmd != NULL) {
		xrt_device_destroy((struct xrt_device **)&sys->hmd);
	}
	rift_s_system_reference(&sys, NULL);
	return NULL;
}

static void
rift_s_system_free(struct rift_s_system *sys)
{
	/* Stop the packet reading thread */
	os_thread_helper_destroy(&sys->oth);

	/* Stop all the frame processing (has to happen before the cameras
	 * and tracker are destroyed */
	xrt_frame_context_destroy_nodes(&sys->xfctx);

	rift_s_radio_state_clear(&sys->radio_state);

	if (sys->handles[HMD_HID]) {
		if (rift_s_hmd_enable(sys->handles[HMD_HID], false) < 0) {
			RIFT_S_WARN("Failed to disable Rift S");
		}
	}

	for (int i = 0; i < 3; i++) {
		if (sys->handles[i] != NULL)
			os_hid_destroy(sys->handles[i]);
	}

	/* Free the camera */
	if (sys->cam != NULL) {
		rift_s_camera_destroy(sys->cam);
	}

	if (sys->tracker != NULL) {
		rift_s_tracker_destroy(sys->tracker);
	}

	os_mutex_destroy(&sys->dev_mutex);

	free(sys);
}

/* Reference count handling for rift_s_system */
void
rift_s_system_reference(struct rift_s_system **dst, struct rift_s_system *src)
{
	struct rift_s_system *old_dst = *dst;

	if (old_dst == src) {
		return;
	}

	if (src) {
		xrt_reference_inc(&src->ref);
	}

	*dst = src;

	if (old_dst) {
		if (xrt_reference_dec(&old_dst->ref)) {
			rift_s_system_free(old_dst);
		}
	}
}

struct os_hid_device *
rift_s_system_hid_handle(struct rift_s_system *sys)
{
	return sys->handles[HMD_HID];
}

rift_s_radio_state *
rift_s_system_radio(struct rift_s_system *sys)
{
	return &sys->radio_state;
}

struct rift_s_tracker *
rift_s_system_get_tracker(struct rift_s_system *sys)
{
	return sys->tracker;
}

struct xrt_device *
rift_s_system_get_hmd(struct rift_s_system *sys)
{
	return (struct xrt_device *)sys->hmd;
}

void
rift_s_system_remove_hmd(struct rift_s_system *sys)
{
	os_mutex_lock(&sys->dev_mutex);
	sys->hmd = NULL;
	os_mutex_unlock(&sys->dev_mutex);
}

struct xrt_device *
rift_s_system_get_controller(struct rift_s_system *sys, int index)
{
	assert(index >= 0 || index < MAX_TRACKED_DEVICES);
	assert(sys->controllers[index] == NULL); // Ensure only called once per controller

	os_mutex_lock(&sys->dev_mutex);
	if (index == 0) {
		sys->controllers[0] = rift_s_controller_create(sys, XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER);
	} else {
		sys->controllers[1] = rift_s_controller_create(sys, XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER);
	}
	os_mutex_unlock(&sys->dev_mutex);

	return (struct xrt_device *)sys->controllers[index];
}

void
rift_s_system_remove_controller(struct rift_s_system *sys, struct rift_s_controller *ctrl)
{
	os_mutex_lock(&sys->dev_mutex);

	for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
		if (sys->controllers[i] == ctrl) {
			sys->controllers[i] = NULL;
			break;
		}
	}

	os_mutex_unlock(&sys->dev_mutex);
}

struct xrt_device *
rift_s_system_get_hand_tracking_device(struct rift_s_system *sys)
{
	return rift_s_tracker_get_hand_tracking_device(sys->tracker);
}

/* Packet reading / handling */
static int
update_tracked_device_types(struct rift_s_system *sys)
{
	int res;
	rift_s_devices_list_t dev_list;
	struct os_hid_device *hid = sys->handles[HMD_HID];

	res = rift_s_read_devices_list(hid, &dev_list);
	if (res < 0)
		return res;

	for (int i = 0; i < dev_list.num_devices; i++) {
		rift_s_device_type_record_t *dev = dev_list.devices + i;
		int d;

		for (d = 0; d < sys->num_active_tracked_devices; d++) {
			if (sys->tracked_device[d].device_id == dev->device_id) {
				if (sys->tracked_device[d].device_type != dev->device_type) {
					sys->tracked_device[d].device_type = dev->device_type;
					RIFT_S_DEBUG("Tracked device 0x%16" PRIx64 " type %u now online",
					             dev->device_id, dev->device_type);
				}
				break;
			}
		}

		if (d == sys->num_active_tracked_devices) {
			RIFT_S_WARN("Got a device type record for an unknown device 0x%16" PRIx64 "\n", dev->device_id);
		}
	}

	return 0;
}

static void
handle_hmd_report(struct rift_s_system *sys, timepoint_ns local_ts, const unsigned char *buf, int size)
{
	rift_s_hmd_report_t report;

	if (!rift_s_parse_hmd_report(&report, buf, size)) {
		return;
	}

	os_mutex_lock(&sys->dev_mutex);
	if (sys->hmd != NULL) {
		rift_s_hmd_handle_report(sys->hmd, local_ts, &report);
	}
	os_mutex_unlock(&sys->dev_mutex);
}

static void
handle_controller_report(struct rift_s_system *sys, timepoint_ns local_ts, const unsigned char *buf, int size)
{
	rift_s_controller_report_t report;

	if (!rift_s_parse_controller_report(&report, buf, size)) {
		rift_s_hexdump_buffer("Invalid Controller Report", buf, size);
		return;
	}

	if (report.device_id == 0x00) {
		/* Dummy report. Ignore it */
		return;
	}

	int i;
	struct rift_s_tracked_device *td = NULL;

	for (i = 0; i < sys->num_active_tracked_devices; i++) {
		if (sys->tracked_device[i].device_id == report.device_id) {
			td = sys->tracked_device + i;
			break;
		}
	}

	if (td == NULL) {
		if (sys->num_active_tracked_devices == MAX_TRACKED_DEVICES) {
			RIFT_S_ERROR("Too many controllers. Can't add %08" PRIx64 "\n", report.device_id);
			return;
		}

		/* Add a new controller to the online list */
		td = sys->tracked_device + sys->num_active_tracked_devices;
		sys->num_active_tracked_devices++;

		memset(td, 0, sizeof(struct rift_s_tracked_device));
		td->device_id = report.device_id;

		update_tracked_device_types(sys);
	}

	struct rift_s_controller *ctrl = NULL;

	os_mutex_lock(&sys->dev_mutex);

	switch (td->device_type) {
	/* If we didn't already succeed in reading the type for this device, try again */
	case RIFT_S_DEVICE_TYPE_UNKNOWN: update_tracked_device_types(sys); break;
	case RIFT_S_DEVICE_LEFT_CONTROLLER: ctrl = sys->controllers[0]; break;
	case RIFT_S_DEVICE_RIGHT_CONTROLLER: ctrl = sys->controllers[1]; break;
	default: break; /* Ignore unknown device type */
	}

	if (ctrl != NULL) {
		rift_s_controller_update_configuration(ctrl, td->device_id);

		if (!rift_s_controller_handle_report(ctrl, local_ts, &report)) {
			rift_s_hexdump_buffer("Invalid Controller Report Content", buf, size);
		}
	}
	os_mutex_unlock(&sys->dev_mutex);
}

static bool
handle_packets(struct rift_s_system *sys)
{
	unsigned char buf[FEATURE_BUFFER_SIZE];
	bool ret = true;

	// Handle keep alive messages
	timepoint_ns now = os_monotonic_get_ns();

	if ((now - sys->last_keep_alive) / U_TIME_1MS_IN_NS >= KEEPALIVE_INTERVAL_MS) {
		// send keep alive message
		rift_s_send_keepalive(sys->handles[HMD_HID]);
		// Update the time of the last keep alive we have sent.
		sys->last_keep_alive = now;
	}

	/* Poll each of the 3 HID interfaces for messages and process them */
	for (int i = 0; i < 3; i++) {
		if (sys->handles[i] == NULL)
			continue;

		while (ret) {
			int size = os_hid_read(sys->handles[i], buf, FEATURE_BUFFER_SIZE, 0);
			if (size < 0) {
				RIFT_S_ERROR("error reading from HMD device");
				ret = false;
				break;
			} else if (size == 0) {
				break; // No more messages, return.
			}

			now = os_monotonic_get_ns();

			if (buf[0] == 0x65)
				handle_hmd_report(sys, now, buf, size);
			else if (buf[0] == 0x67)
				handle_controller_report(sys, now, buf, size);
			else if (buf[0] == 0x66) {
				/* System state packet. Enable the screen if the prox sensor is
				 * triggered. */
				bool prox_sensor = (buf[1] == 0) ? false : true;
				os_mutex_lock(&sys->dev_mutex);
				if (sys->hmd != NULL) {
					rift_s_hmd_set_proximity(sys->hmd, prox_sensor);
				}
				os_mutex_unlock(&sys->dev_mutex);
			} else {
				RIFT_S_WARN("Unknown Rift S report 0x%02x!", buf[0]);
			}
		}
	}

	return ret;
}


static void *
rift_s_run_thread(void *ptr)
{
	DRV_TRACE_MARKER();

	struct rift_s_system *sys = (struct rift_s_system *)ptr;

	os_thread_helper_lock(&sys->oth);
	while (os_thread_helper_is_running_locked(&sys->oth)) {
		os_thread_helper_unlock(&sys->oth);

		bool success = handle_packets(sys);

		if (success) {
			rift_s_radio_update(&sys->radio_state, sys->handles[HMD_HID]);

			os_mutex_lock(&sys->dev_mutex);
			if (sys->cam != NULL) {
				rift_s_camera_update(sys->cam, sys->handles[HMD_HID]);
			}
			os_mutex_unlock(&sys->dev_mutex);
		}

		os_thread_helper_lock(&sys->oth);

		if (!success) {
			break;
		}

		if (os_thread_helper_is_running_locked(&sys->oth)) {
			os_nanosleep(U_TIME_1MS_IN_NS / 2);
		}
	}
	os_thread_helper_unlock(&sys->oth);

	RIFT_S_DEBUG("Exiting packet reading thread");

	return NULL;
}
