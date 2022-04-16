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
 * @brief  Driver code for Oculus Rift S headsets
 *
 * Implementation for the HMD communication, calibration and
 * IMU integration.
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
#include "util/u_trace_marker.h"
#include "util/u_var.h"

#include "xrt/xrt_device.h"

#include "rift_s_hmd.h"

#define DEG_TO_RAD(D) ((D)*M_PI / 180.)

static void
rift_s_update_inputs(struct xrt_device *xdev)
{}

static void
rift_s_get_tracked_pose(struct xrt_device *xdev,
                        enum xrt_input_name name,
                        uint64_t at_timestamp_ns,
                        struct xrt_space_relation *out_relation)
{
	struct rift_s_hmd *hmd = (struct rift_s_hmd *)(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		RIFT_S_ERROR("Unknown input name");
		return;
	}

	U_ZERO(out_relation);

	// Estimate pose at timestamp at_timestamp_ns!
	os_mutex_lock(&hmd->mutex);
	math_quat_normalize(&hmd->pose.orientation);
	out_relation->pose = hmd->pose;
	out_relation->relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_POSITION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
	os_mutex_unlock(&hmd->mutex);
}

static void
rift_s_get_view_poses(struct xrt_device *xdev,
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

void
rift_s_hmd_handle_report(struct rift_s_hmd *hmd, timepoint_ns local_ts, rift_s_hmd_report_t *report)
{
	const uint32_t TICK_LEN_US = 1000000 / hmd->imu_config.imu_hz;
	uint32_t dt = TICK_LEN_US;

	os_mutex_lock(&hmd->mutex);

	if (hmd->last_imu_timestamp_ns != 0) {
		/* Avoid wrap-around on 32-bit device times */
		dt = report->timestamp - hmd->last_imu_timestamp32;
	} else {
		hmd->last_imu_timestamp_ns = report->timestamp;
	}
	hmd->last_imu_timestamp32 = report->timestamp;
	hmd->last_imu_local_timestamp_ns = local_ts;

	const float gyro_scale = 1.0 / hmd->imu_config.gyro_scale;
	const float accel_scale = MATH_GRAVITY_M_S2 / hmd->imu_config.accel_scale;
	const float temperature_scale = 1.0 / hmd->imu_config.temperature_scale;
	const float temperature_offset = hmd->imu_config.temperature_offset;

	for (int i = 0; i < 3; i++) {
		rift_s_hmd_imu_sample_t *s = report->samples + i;

		if (s->marker & 0x80)
			break; /* Sample (and remaining ones) are invalid */

		struct xrt_vec3 gyro, accel;

		gyro.x = DEG_TO_RAD(gyro_scale * s->gyro[0]);
		gyro.y = DEG_TO_RAD(gyro_scale * s->gyro[1]);
		gyro.z = DEG_TO_RAD(gyro_scale * s->gyro[2]);

		accel.x = accel_scale * s->accel[0];
		accel.y = accel_scale * s->accel[1];
		accel.z = accel_scale * s->accel[2];

		/* Apply correction offsets first, then rectify */
		accel = m_vec3_sub(accel, hmd->imu_calibration.accel.offset_at_0C);
		gyro = m_vec3_sub(gyro, hmd->imu_calibration.gyro.offset);

		math_matrix_3x3_transform_vec3(&hmd->imu_calibration.accel.rectification, &accel, &hmd->raw_accel);
		math_matrix_3x3_transform_vec3(&hmd->imu_calibration.gyro.rectification, &gyro, &hmd->raw_gyro);

		/* FIXME: This doesn't seem to produce the right numbers, but it's OK - we don't use it anyway */
		hmd->temperature = temperature_scale * (s->temperature - temperature_offset) + 25;

#if 0
		printf ("Sample %d dt %f accel %f %f %f gyro %f %f %f\n",
			i, dt_sec, hmd->raw_accel.x, hmd->raw_accel.y, hmd->raw_accel.z,
			hmd->raw_gyro.x, hmd->raw_gyro.y, hmd->raw_gyro.z);
#endif

		// Do 3DOF fusion
		m_imu_3dof_update(&hmd->fusion, hmd->last_imu_timestamp_ns, &hmd->raw_accel, &hmd->raw_gyro);

		hmd->last_imu_timestamp_ns += (uint64_t)dt * OS_NS_PER_USEC;
		dt = TICK_LEN_US;
	}

	hmd->pose.orientation = hmd->fusion.rot;
	os_mutex_unlock(&hmd->mutex);
}

static bool
rift_s_compute_distortion(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result)
{
	struct rift_s_hmd *hmd = (struct rift_s_hmd *)(xdev);
	return u_compute_distortion_panotools(&hmd->distortion_vals[view], u, v, result);
}

#if 0
static int
dump_fw_block(struct os_hid_device *handle, uint8_t block_id) {
	int res;
	char *data = NULL;
	int len;

	res = rift_s_read_firmware_block (handle, block_id, &data, &len);
	if (res	< 0)
			return res;

	free (data);
	return 0;
}
#endif

static int
read_hmd_calibration(struct rift_s_hmd *hmd, struct os_hid_device *hid_hmd)
{
	char *json = NULL;
	int json_len = 0;

	int ret = rift_s_read_firmware_block(hid_hmd, RIFT_S_FIRMWARE_BLOCK_IMU_CALIB, &json, &json_len);
	if (ret < 0)
		return ret;

	ret = rift_s_parse_imu_calibration(json, &hmd->imu_calibration);
	free(json);

	if (ret < 0)
		return ret;

	ret = rift_s_read_firmware_block(hid_hmd, RIFT_S_FIRMWARE_BLOCK_CAMERA_CALIB, &json, &json_len);
	if (ret < 0)
		return ret;

	ret = rift_s_parse_camera_calibration_block(json, &hmd->camera_calibration);
	free(json);

	return ret;
}

static int
rift_s_hmd_read_proximity_threshold(struct rift_s_hmd *hmd, struct os_hid_device *hid_hmd)
{
	char *json = NULL;
	int json_len = 0;

	int ret = rift_s_read_firmware_block(hid_hmd, RIFT_S_FIRMWARE_BLOCK_THRESHOLD, &json, &json_len);
	if (ret < 0)
		return ret;

	ret = rift_s_parse_proximity_threshold(json, &hmd->proximity_threshold);
	free(json);

	return ret;
}

static void
rift_s_hmd_destroy(struct xrt_device *xdev)
{
	struct rift_s_hmd *hmd = (struct rift_s_hmd *)(xdev);

	DRV_TRACE_MARKER();

	/* Remove this device from the system */
	rift_s_system_remove_hmd(hmd->sys);

	/* Drop the reference to the system */
	rift_s_system_reference(&hmd->sys, NULL);

	u_var_remove_root(hmd);

	m_imu_3dof_close(&hmd->fusion);

	os_mutex_destroy(&hmd->mutex);

	u_device_free(&hmd->base);
}

struct rift_s_hmd *
rift_s_hmd_create(struct rift_s_system *sys, const unsigned char *hmd_serial_no)
{
	int ret;

	DRV_TRACE_MARKER();

	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);

	struct rift_s_hmd *hmd = U_DEVICE_ALLOCATE(struct rift_s_hmd, flags, 1, 0);
	if (hmd == NULL) {
		return NULL;
	}

	/* Take a reference to the rift_s_system */
	rift_s_system_reference(&hmd->sys, sys);

	hmd->base.tracking_origin = &sys->base;

	hmd->base.update_inputs = rift_s_update_inputs;
	hmd->base.get_tracked_pose = rift_s_get_tracked_pose;
	hmd->base.get_view_poses = rift_s_get_view_poses;
	hmd->base.destroy = rift_s_hmd_destroy;
	hmd->base.name = XRT_DEVICE_GENERIC_HMD;
	hmd->base.device_type = XRT_DEVICE_TYPE_HMD;
	hmd->pose.orientation.w = 1.0f; // All other values set to zero by U_DEVICE_ALLOCATE (which calls U_CALLOC)

	m_imu_3dof_init(&hmd->fusion, M_IMU_3DOF_USE_GRAVITY_DUR_20MS);

	// Pose / state lock
	ret = os_mutex_init(&hmd->mutex);
	if (ret != 0) {
		RIFT_S_ERROR("Failed to init mutex!");
		goto cleanup;
	}

	// Print name.
	snprintf(hmd->base.str, XRT_DEVICE_NAME_LEN, "Oculus Rift S");
	snprintf(hmd->base.serial, XRT_DEVICE_NAME_LEN, "%s", hmd_serial_no);

	// Setup input.
	hmd->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	hmd->last_imu_timestamp_ns = 0;

	struct os_hid_device *hid_hmd = rift_s_system_hid_handle(hmd->sys);

	if (rift_s_read_panel_info(hid_hmd, &hmd->panel_info) < 0) {
		RIFT_S_ERROR("Failed to read Rift S device info");
		goto cleanup;
	}

	if (rift_s_read_firmware_version(hid_hmd) < 0) {
		RIFT_S_ERROR("Failed to read Rift S firmware version");
		goto cleanup;
	}

	if (rift_s_read_imu_config(hid_hmd, &hmd->imu_config) < 0) {
		RIFT_S_ERROR("Failed to read IMU configuration block");
		goto cleanup;
	}

	if (read_hmd_calibration(hmd, hid_hmd) < 0)
		goto cleanup;

	/* Configure the proximity sensor threshold */
	if (rift_s_hmd_read_proximity_threshold(hmd, hid_hmd) < 0)
		goto cleanup;

	RIFT_S_DEBUG("Configuring firmware provided proximity sensor threshold %u", hmd->proximity_threshold);

	if (rift_s_protocol_set_proximity_threshold(hid_hmd, (uint16_t)hmd->proximity_threshold) < 0)
		goto cleanup;

#if 0
	dump_fw_block(hid_hmd, 0xB);
	dump_fw_block(hid_hmd, 0xF);
	dump_fw_block(hid_hmd, 0x10);
	dump_fw_block(hid_hmd, 0x12);
#endif

	// Set up display details
	// FIXME: These are all wrong and should be derived from HMD reports
	// refresh rate
	hmd->base.hmd->screens[0].nominal_frame_interval_ns = time_s_to_ns(1.0f / 80.0f);

	/* In the Rift S, there's one panel that is rotated
	 * to the right, so reported to the OS as a
	 * 1440x2560 wxh panel that needs to be split
	 * in two and each view rotated for rendering */
	const int view_w = 1440;
	const int view_h = 1280;

	/* screen is the physical width/height of the panel
	 * as presented to the OS */
	hmd->base.hmd->screens[0].w_pixels = view_w;
	hmd->base.hmd->screens[0].h_pixels = view_h * 2;

	// Left, Right eye view setup
	for (uint8_t eye = 0; eye < 2; ++eye) {
		// Display w/h need to be swapped, as the client sees / renders
		hmd->base.hmd->views[eye].display.w_pixels = view_h;
		hmd->base.hmd->views[eye].display.h_pixels = view_w;
		// Viewport is position on the output panel
		hmd->base.hmd->views[eye].viewport.y_pixels = 0;
		hmd->base.hmd->views[eye].viewport.w_pixels = view_w;
		hmd->base.hmd->views[eye].viewport.h_pixels = view_h;
		hmd->base.hmd->views[eye].rot = u_device_rotation_right;
	}
	// left eye starts at y=0, right eye starts at y=view_height
	hmd->base.hmd->views[0].viewport.y_pixels = 0;
	hmd->base.hmd->views[1].viewport.y_pixels = view_h;

	/* FIXME: Incorrection distortion taken from the Rift CV1 for now */
	const double display_w_meters = 0.149760f / 2.0; // Per-eye width
	const double display_h_meters = 0.093600f;
	const double lens_sep = 0.074f;
	const double hFOV = DEG_TO_RAD(105.0);

	// center of projection
	const double hCOP = lens_sep / 2.0;
	const double vCOP = display_h_meters / 2.0;

	struct u_panotools_values distortion_vals = {
	    .distortion_k = {0.819f, -0.241f, 0.324f, 0.098f, 0.0},
	    .aberration_k = {0.9952420f, 1.0f, 1.0008074f},
	    .scale = display_w_meters -
	             lens_sep / 2.0, // Assume distortion is across the larger distance from lens center to edge
	    .lens_center = {display_w_meters - hCOP, vCOP},
	    .viewport_size = {display_w_meters, display_h_meters},
	};

	if (
	    /* right eye */
	    !math_compute_fovs(display_w_meters, hCOP, hFOV, display_h_meters, vCOP, 0.0,
	                       &hmd->base.hmd->distortion.fov[1]) ||
	    /*
	     * left eye - same as right eye, except the horizontal center of projection is moved in the opposite
	     * direction now
	     */
	    !math_compute_fovs(display_w_meters, display_w_meters - hCOP, hFOV, display_h_meters, vCOP, 0.0,
	                       &hmd->base.hmd->distortion.fov[0])) {
		// If those failed, it means our math was impossible.
		RIFT_S_ERROR("Failed to setup basic device info");
		goto cleanup;
	}

	hmd->distortion_vals[0] = distortion_vals;
	// Move the lens center for the right view
	distortion_vals.lens_center.x = hCOP;
	hmd->distortion_vals[1] = distortion_vals;

	hmd->base.hmd->distortion.models = XRT_DISTORTION_MODEL_COMPUTE;
	hmd->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_COMPUTE;
	hmd->base.compute_distortion = rift_s_compute_distortion;
	u_distortion_mesh_fill_in_compute(&hmd->base);

	/* Set Opaque blend mode */
	hmd->base.hmd->blend_modes[0] = XRT_BLEND_MODE_OPAQUE;
	hmd->base.hmd->blend_mode_count = 1;

	// Setup variable tracker: Optional but useful for debugging
	u_var_add_root(hmd, "Oculus Rift S", true);

	u_var_add_gui_header(hmd, NULL, "Tracking");
	u_var_add_pose(hmd, &hmd->pose, "pose");

	u_var_add_gui_header(hmd, NULL, "3DoF Tracking");
	m_imu_3dof_add_vars(&hmd->fusion, hmd, "");

	u_var_add_gui_header(hmd, NULL, "Misc");
	u_var_add_log_level(hmd, &rift_s_log_level, "log_level");

	RIFT_S_DEBUG("Oculus Rift S HMD serial %s initialised.", hmd_serial_no);

	return hmd;

cleanup:
	rift_s_system_reference(&hmd->sys, NULL);
	return NULL;
}

void
rift_s_hmd_set_proximity(struct rift_s_hmd *hmd, bool prox_sensor)
{
	/* Enable the screen if the prox sensor is triggered, or turn it off otherwise. */
	if (prox_sensor != hmd->display_on) {
		struct os_hid_device *hid_hmd = rift_s_system_hid_handle(hmd->sys);

		rift_s_set_screen_enable(hid_hmd, prox_sensor);
		hmd->display_on = prox_sensor;
	}
}
