// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Dummy HMD device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_dummy
 */

#include "xrt/xrt_device.h"

#include "os/os_time.h"

#include "math/m_api.h"
#include "math/m_mathinclude.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_logging.h"
#include "util/u_distortion_mesh.h"

#include <stdio.h>


/*
 *
 * Structs and defines.
 *
 */

enum dummy_movement
{
	DUMMY_WOBBLE,
	DUMMY_ROTATE,
};


/*!
 * A example HMD device.
 *
 * @implements xrt_device
 */
struct dummy_hmd
{
	struct xrt_device base;

	struct xrt_pose pose;
	struct xrt_vec3 center;

	uint64_t created_ns;
	float diameter_m;

	enum u_logging_level log_level;
	enum dummy_movement movement;
};


/*
 *
 * Functions
 *
 */

static inline struct dummy_hmd *
dummy_hmd(struct xrt_device *xdev)
{
	return (struct dummy_hmd *)xdev;
}

DEBUG_GET_ONCE_LOG_OPTION(dummy_log, "DUMMY_LOG", U_LOGGING_WARN)
DEBUG_GET_ONCE_BOOL_OPTION(dummy_rotate, "DUMMY_ROTATE", false)

#define DH_TRACE(p, ...) U_LOG_XDEV_IFL_T(&dh->base, dh->log_level, __VA_ARGS__)
#define DH_DEBUG(p, ...) U_LOG_XDEV_IFL_D(&dh->base, dh->log_level, __VA_ARGS__)
#define DH_ERROR(p, ...) U_LOG_XDEV_IFL_E(&dh->base, dh->log_level, __VA_ARGS__)

static void
dummy_hmd_destroy(struct xrt_device *xdev)
{
	struct dummy_hmd *dh = dummy_hmd(xdev);

	// Remove the variable tracking.
	u_var_remove_root(dh);

	u_device_free(&dh->base);
}

static void
dummy_hmd_update_inputs(struct xrt_device *xdev)
{
	// Empty, you should put code to update the attached inputs fields.
}

static void
dummy_hmd_get_tracked_pose(struct xrt_device *xdev,
                           enum xrt_input_name name,
                           uint64_t at_timestamp_ns,
                           struct xrt_space_relation *out_relation)
{
	struct dummy_hmd *dh = dummy_hmd(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		DH_ERROR(dh, "unknown input name");
		return;
	}

	const double time_s = time_ns_to_s(at_timestamp_ns - dh->created_ns);
	const double d = dh->diameter_m;
	const double d2 = d * 2;
	const double t = 2.0;
	const double t2 = t * 2;
	const double t3 = t * 3;
	const double t4 = t * 4;
	const struct xrt_vec3 up = {0, 1, 0};

	switch (dh->movement) {
	default:
	case DUMMY_WOBBLE:
		// Wobble time.
		dh->pose.position.x = dh->center.x + sin((time_s / t2) * M_PI) * d2 - d;
		dh->pose.position.y = dh->center.y + sin((time_s / t) * M_PI) * d;
		dh->pose.orientation.x = sin((time_s / t3) * M_PI) / 64.0;
		dh->pose.orientation.y = sin((time_s / t4) * M_PI) / 16.0;
		dh->pose.orientation.z = sin((time_s / t4) * M_PI) / 64.0;
		dh->pose.orientation.w = 1;
		math_quat_normalize(&dh->pose.orientation);
		break;
	case DUMMY_ROTATE:
		// Reset position.
		dh->pose.position = dh->center;

		// Rotate around the up vector.
		math_quat_from_angle_vector(time_s / 4, &up, &dh->pose.orientation);
		break;
	}

	out_relation->pose = dh->pose;
	out_relation->relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_POSITION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
}

static void
dummy_hmd_get_view_poses(struct xrt_device *xdev,
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

struct xrt_device *
dummy_hmd_create(void)
{
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct dummy_hmd *dh = U_DEVICE_ALLOCATE(struct dummy_hmd, flags, 1, 0);
	dh->base.update_inputs = dummy_hmd_update_inputs;
	dh->base.get_tracked_pose = dummy_hmd_get_tracked_pose;
	dh->base.get_view_poses = dummy_hmd_get_view_poses;
	dh->base.destroy = dummy_hmd_destroy;
	dh->base.name = XRT_DEVICE_GENERIC_HMD;
	dh->base.device_type = XRT_DEVICE_TYPE_HMD;
	dh->pose.orientation.w = 1.0f; // All other values set to zero.
	dh->created_ns = os_monotonic_get_ns();
	dh->diameter_m = 0.05;
	dh->log_level = debug_get_log_option_dummy_log();

	// Print name.
	snprintf(dh->base.str, XRT_DEVICE_NAME_LEN, "Dummy HMD");
	snprintf(dh->base.serial, XRT_DEVICE_NAME_LEN, "Dummy HMD");

	// Setup input.
	dh->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	// Setup info.
	struct u_device_simple_info info;
	info.display.w_pixels = 1280;
	info.display.h_pixels = 720;
	info.display.w_meters = 0.13f;
	info.display.h_meters = 0.07f;
	info.lens_horizontal_separation_meters = 0.13f / 2.0f;
	info.lens_vertical_position_meters = 0.07f / 2.0f;
	info.views[0].fov = 85.0f * (M_PI / 180.0f);
	info.views[1].fov = 85.0f * (M_PI / 180.0f);

	if (!u_device_setup_split_side_by_side(&dh->base, &info)) {
		DH_ERROR(dh, "Failed to setup basic device info");
		dummy_hmd_destroy(&dh->base);
		return NULL;
	}

	// Select the type of movement.
	dh->movement = DUMMY_WOBBLE;
	if (debug_get_bool_option_dummy_rotate()) {
		dh->movement = DUMMY_ROTATE;
	}

	// Setup variable tracker.
	u_var_add_root(dh, "Dummy HMD", true);
	u_var_add_pose(dh, &dh->pose, "pose");
	u_var_add_vec3_f32(dh, &dh->center, "center");
	u_var_add_f32(dh, &dh->diameter_m, "diameter_m");
	u_var_add_log_level(dh, &dh->log_level, "log_level");

	// Distortion information, fills in xdev->compute_distortion().
	u_distortion_mesh_set_none(&dh->base);

	return &dh->base;
}
