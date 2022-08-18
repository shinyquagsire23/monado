// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Sample HMD device, use as a starting point to make your own device driver.
 *
 *
 * Based largely on simulated_hmd.c
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_sample
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

/*!
 * A sample HMD device.
 *
 * @implements xrt_device
 */
struct sample_hmd
{
	struct xrt_device base;

	struct xrt_pose pose;

	enum u_logging_level log_level;
};


/// Casting helper function
static inline struct sample_hmd *
sample_hmd(struct xrt_device *xdev)
{
	return (struct sample_hmd *)xdev;
}

DEBUG_GET_ONCE_LOG_OPTION(sample_log, "SAMPLE_LOG", U_LOGGING_WARN)

#define SH_TRACE(p, ...) U_LOG_XDEV_IFL_T(&sh->base, sh->log_level, __VA_ARGS__)
#define SH_DEBUG(p, ...) U_LOG_XDEV_IFL_D(&sh->base, sh->log_level, __VA_ARGS__)
#define SH_ERROR(p, ...) U_LOG_XDEV_IFL_E(&sh->base, sh->log_level, __VA_ARGS__)

static void
sample_hmd_destroy(struct xrt_device *xdev)
{
	struct sample_hmd *sh = sample_hmd(xdev);

	// Remove the variable tracking.
	u_var_remove_root(sh);

	u_device_free(&sh->base);
}

static void
sample_hmd_update_inputs(struct xrt_device *xdev)
{
	// Empty, you should put code to update the attached input fields (if any)
}

static void
sample_hmd_get_tracked_pose(struct xrt_device *xdev,
                            enum xrt_input_name name,
                            uint64_t at_timestamp_ns,
                            struct xrt_space_relation *out_relation)
{
	struct sample_hmd *sh = sample_hmd(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		SH_ERROR(sh, "unknown input name");
		return;
	}

	// Estimate pose at timestamp at_timestamp_ns!
	math_quat_normalize(&sh->pose.orientation);
	out_relation->pose = sh->pose;
	out_relation->relation_flags = (enum xrt_space_relation_flags)(XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_POSITION_VALID_BIT |
	                                                               XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT);
}

static void
sample_hmd_get_view_poses(struct xrt_device *xdev,
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
sample_hmd_create(void)
{
	// This indicates you won't be using Monado's built-in tracking algorithms.
	enum u_device_alloc_flags flags =
	    (enum u_device_alloc_flags)(U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);

	struct sample_hmd *sh = U_DEVICE_ALLOCATE(struct sample_hmd, flags, 1, 0);

	// This list should be ordered, most preferred first.
	size_t idx = 0;
	sh->base.hmd->blend_modes[idx++] = XRT_BLEND_MODE_OPAQUE;
	sh->base.hmd->blend_mode_count = idx;

	sh->base.update_inputs = sample_hmd_update_inputs;
	sh->base.get_tracked_pose = sample_hmd_get_tracked_pose;
	sh->base.get_view_poses = sample_hmd_get_view_poses;
	sh->base.destroy = sample_hmd_destroy;

	sh->pose = (struct xrt_pose)XRT_POSE_IDENTITY;
	sh->log_level = debug_get_log_option_sample_log();

	// Print name.
	snprintf(sh->base.str, XRT_DEVICE_NAME_LEN, "Sample HMD");
	snprintf(sh->base.serial, XRT_DEVICE_NAME_LEN, "Sample HMD S/N");

	// Setup input.
	sh->base.name = XRT_DEVICE_GENERIC_HMD;
	sh->base.device_type = XRT_DEVICE_TYPE_HMD;
	sh->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	sh->base.orientation_tracking_supported = true;
	sh->base.position_tracking_supported = false;

	// Set up display details
	// refresh rate
	sh->base.hmd->screens[0].nominal_frame_interval_ns = time_s_to_ns(1.0f / 90.0f);

	const double hFOV = 90 * (M_PI / 180.0);
	const double vFOV = 96.73 * (M_PI / 180.0);
	// center of projection
	const double hCOP = 0.529;
	const double vCOP = 0.5;
	if (
	    /* right eye */
	    !math_compute_fovs(1, hCOP, hFOV, 1, vCOP, vFOV, &sh->base.hmd->distortion.fov[1]) ||
	    /*
	     * left eye - same as right eye, except the horizontal center of projection is moved in the opposite
	     * direction now
	     */
	    !math_compute_fovs(1, 1.0 - hCOP, hFOV, 1, vCOP, vFOV, &sh->base.hmd->distortion.fov[0])) {
		// If those failed, it means our math was impossible.
		SH_ERROR(sh, "Failed to setup basic device info");
		sample_hmd_destroy(&sh->base);
		return NULL;
	}
	const int panel_w = 1080;
	const int panel_h = 1200;

	// Single "screen" (always the case)
	sh->base.hmd->screens[0].w_pixels = panel_w * 2;
	sh->base.hmd->screens[0].h_pixels = panel_h;

	// Left, Right
	for (uint8_t eye = 0; eye < 2; ++eye) {
		sh->base.hmd->views[eye].display.w_pixels = panel_w;
		sh->base.hmd->views[eye].display.h_pixels = panel_h;
		sh->base.hmd->views[eye].viewport.y_pixels = 0;
		sh->base.hmd->views[eye].viewport.w_pixels = panel_w;
		sh->base.hmd->views[eye].viewport.h_pixels = panel_h;
		// if rotation is not identity, the dimensions can get more complex.
		sh->base.hmd->views[eye].rot = u_device_rotation_ident;
	}
	// left eye starts at x=0, right eye starts at x=panel_width
	sh->base.hmd->views[0].viewport.x_pixels = 0;
	sh->base.hmd->views[1].viewport.x_pixels = panel_w;

	// Distortion information, fills in xdev->compute_distortion().
	u_distortion_mesh_set_none(&sh->base);

	// Setup variable tracker: Optional but useful for debugging
	u_var_add_root(sh, "Sample HMD", true);
	u_var_add_pose(sh, &sh->pose, "pose");
	u_var_add_log_level(sh, &sh->log_level, "log_level");


	return &sh->base;
}
