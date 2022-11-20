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
 * @brief  Driver code for Meta Quest Link headsets
 *
 * Implementation for the HMD communication, calibration and
 * IMU integration.
 *
 * Ported from OpenHMD
 *
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_quest_link
 */

/* Meta Quest Link Driver - HID/USB Driver Implementation */

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
#include "util/u_time.h"
#include "util/u_var.h"

#include "xrt/xrt_device.h"

#include "ql_hmd.h"
#include "ql_system.h"

#include "ql_comp_target.h"

static void
ql_update_inputs(struct xrt_device *xdev)
{}

static void ql_hmd_create_compositor_target(struct xrt_device * xdev,
                                               struct comp_compositor * comp,
                                               struct comp_target ** out_target);

static void
ql_get_tracked_pose(struct xrt_device *xdev,
                        enum xrt_input_name name,
                        uint64_t at_timestamp_ns,
                        struct xrt_space_relation *out_relation)
{
	struct ql_hmd *hmd = (struct ql_hmd *)(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		QUEST_LINK_ERROR("Unknown input name");
		return;
	}

	U_ZERO(out_relation);

	const double time_s = time_ns_to_s(at_timestamp_ns - hmd->created_ns);
	const double d = 0.05;//hmd->diameter_m;
	const double d2 = d * 2;
	const double t = 2.0;
	const double t2 = t * 2;
	const double t3 = t * 3;
	const double t4 = t * 4;
	const struct xrt_vec3 up = {0, 1, 0};

	// Wobble time.
	//hmd->pose.position.x = hmd->center.x + sin((time_s / t2) * M_PI) * d2 - d;
	//hmd->pose.position.y = hmd->center.y + sin((time_s / t) * M_PI) * d;
	
	/*hmd->pose.orientation.x = sin((time_s / t3) * M_PI) / 64.0f;
	hmd->pose.orientation.y = sin((time_s / t4) * M_PI) / 16.0f;
	hmd->pose.orientation.z = sin((time_s / t4) * M_PI) / 64.0f;
	hmd->pose.orientation.w = 1;*/
	math_quat_normalize(&hmd->pose.orientation);

	out_relation->pose = hmd->pose;
	out_relation->relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_POSITION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);

	//ql_tracker_get_tracked_pose(hmd->tracker, RIFT_S_TRACKER_POSE_DEVICE, at_timestamp_ns, out_relation);
}

static void
ql_get_view_poses(struct xrt_device *xdev,
                      const struct xrt_vec3 *default_eye_relation,
                      uint64_t at_timestamp_ns,
                      uint32_t view_count,
                      struct xrt_space_relation *out_head_relation,
                      struct xrt_fov *out_fovs,
                      struct xrt_pose *out_poses)
{
	struct ql_hmd *hmd = (struct ql_hmd *)(xdev);

	static float test = 0.0;
	//test += 0.001;
	struct xrt_vec3 modify_eye_relation = *default_eye_relation;
	modify_eye_relation.x = hmd->ipd_meters;
	//printf("%f\n", modify_eye_relation.x);

	u_device_get_view_poses(xdev, &modify_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs,
	                        out_poses);
}

static void
ql_hmd_destroy(struct xrt_device *xdev)
{
	struct ql_hmd *hmd = (struct ql_hmd *)(xdev);

	DRV_TRACE_MARKER();

	/* Remove this device from the system */
	ql_system_remove_hmd(hmd->sys);

	/* Drop the reference to the system */
	ql_system_reference(&hmd->sys, NULL);

	u_var_remove_root(hmd);

	u_device_free(&hmd->base);
}

void ql_hmd_set_per_eye_resolution(struct ql_hmd* hmd, uint32_t w, uint32_t h, float fps)
{
	auto eye_width = w;
	auto eye_height = h;

	// Setup info.
	hmd->base.hmd->blend_modes[0] = XRT_BLEND_MODE_OPAQUE;
	hmd->base.hmd->blend_mode_count = 1;
	hmd->base.hmd->distortion.models = XRT_DISTORTION_MODEL_NONE;
	hmd->base.hmd->distortion.preferred = XRT_DISTORTION_MODEL_NONE;

	hmd->base.hmd->screens[0].w_pixels = eye_width * 2;
	hmd->base.hmd->screens[0].h_pixels = eye_height;
	hmd->base.hmd->screens[0].nominal_frame_interval_ns = 1000000000 / fps;

	// Left
	hmd->base.hmd->views[0].display.w_pixels = eye_width;
	hmd->base.hmd->views[0].display.h_pixels = eye_height;
	hmd->base.hmd->views[0].viewport.x_pixels = 0;
	hmd->base.hmd->views[0].viewport.y_pixels = 0;
	hmd->base.hmd->views[0].viewport.w_pixels = eye_width;
	hmd->base.hmd->views[0].viewport.h_pixels = eye_height;
	hmd->base.hmd->views[0].rot = u_device_rotation_ident;

	// Right
	hmd->base.hmd->views[1].display.w_pixels = eye_width;
	hmd->base.hmd->views[1].display.h_pixels = eye_height;
	hmd->base.hmd->views[1].viewport.x_pixels = eye_width;
	hmd->base.hmd->views[1].viewport.y_pixels = 0;
	hmd->base.hmd->views[1].viewport.w_pixels = eye_width;
	hmd->base.hmd->views[1].viewport.h_pixels = eye_height;
	hmd->base.hmd->views[1].rot = u_device_rotation_ident;

	hmd->encode_width = eye_width * 2;
	hmd->encode_height = eye_height;
}

struct ql_hmd *
ql_hmd_create(struct ql_system *sys, const unsigned char *hmd_serial_no, struct ql_hmd_config *config)
{
	DRV_TRACE_MARKER();

	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);

	struct ql_hmd *hmd = U_DEVICE_ALLOCATE(struct ql_hmd, flags, 1, 0);
	if (hmd == NULL) {
		return NULL;
	}

	/* Take a reference to the ql_system */
	ql_system_reference(&hmd->sys, sys);

	hmd->config = config;

	hmd->base.tracking_origin = &sys->base;

	hmd->base.update_inputs = ql_update_inputs;
	hmd->base.get_tracked_pose = ql_get_tracked_pose;
	hmd->base.get_view_poses = ql_get_view_poses;
	hmd->base.create_compositor_target = ql_hmd_create_compositor_target;
	hmd->base.destroy = ql_hmd_destroy;
	hmd->base.name = XRT_DEVICE_GENERIC_HMD;
	hmd->base.device_type = XRT_DEVICE_TYPE_HMD;

	//hmd->tracker = ql_system_get_tracker(sys);

	// Print name.
	snprintf(hmd->base.str, XRT_DEVICE_NAME_LEN, "Meta Quest Link");
	snprintf(hmd->base.serial, XRT_DEVICE_NAME_LEN, "%s", hmd_serial_no);

	// Setup input.
	hmd->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	hmd->created_ns = os_monotonic_get_ns();
	hmd->last_imu_timestamp_ns = 0;

	hmd->pose.position.x = 0.0f;
	hmd->pose.position.y = 0.0f;
	hmd->pose.position.z = 0.0f;

	hmd->pose.orientation.x = 0.0f;
	hmd->pose.orientation.y = 0.0f;
	hmd->pose.orientation.z = 0.0f;
	hmd->pose.orientation.w = 1.0f;



	auto eye_width = 3616/2;
	auto eye_height = 1920;
	ql_hmd_set_per_eye_resolution(hmd, eye_width, eye_height, 72.0);

	// Default FOV from Oculus Quest
	hmd->base.hmd->distortion.fov[0].angle_up = 48 * M_PI / 180;
	hmd->base.hmd->distortion.fov[0].angle_down = -50 * M_PI / 180;
	hmd->base.hmd->distortion.fov[0].angle_left = -52 * M_PI / 180;
	hmd->base.hmd->distortion.fov[0].angle_right = 45 * M_PI / 180;
	
	hmd->base.hmd->distortion.fov[1].angle_up = 48 * M_PI / 180;
	hmd->base.hmd->distortion.fov[1].angle_down = -50 * M_PI / 180;
	hmd->base.hmd->distortion.fov[1].angle_left = -45 * M_PI / 180;
	hmd->base.hmd->distortion.fov[1].angle_right = 52 * M_PI / 180;

	hmd->ipd_meters = 0.063;


#if 0
	// Setup info.
	struct u_device_simple_info info;
	info.display.w_pixels = 3616;
	info.display.h_pixels = 1920;
	info.display.w_meters = 0.13f;
	info.display.h_meters = 0.07f;
	info.lens_horizontal_separation_meters = 0.13f / 2.0f;
	info.lens_vertical_position_meters = 0.07f / 2.0f;
	info.fov[0] = 85.0f * ((float)(M_PI) / 180.0f);
	info.fov[1] = 85.0f * ((float)(M_PI) / 180.0f);

	if (!u_device_setup_split_side_by_side(&hmd->base, &info)) {
		QUEST_LINK_ERROR("Failed to setup basic device info");
		ql_hmd_destroy(&hmd->base);
		return NULL;
	}
#endif

	u_distortion_mesh_set_none(&hmd->base);
	

	u_var_add_gui_header(hmd, NULL, "Misc");
	//u_var_add_log_level(hmd, &ql_log_level, "log_level");

	QUEST_LINK_DEBUG("Meta Quest Link HMD serial %s initialised.", hmd_serial_no);

	return hmd;

cleanup:
	ql_system_reference(&hmd->sys, NULL);
	return NULL;
}

static void ql_hmd_create_compositor_target(struct xrt_device * xdev,
                                               struct comp_compositor * comp,
                                               struct comp_target ** out_target)
{
	struct ql_hmd *hmd = (struct ql_hmd *)(xdev);
	while (!hmd->sys->xrsp_host.ready_to_send_frames) {
		os_nanosleep(U_TIME_1MS_IN_NS * 10);
	}

	comp_target* target = comp_target_ql_create(&hmd->sys->xrsp_host, 72);

	target->c = comp;
	*out_target = target;
}

