// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  HMD remote driver.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_remote
 */

#include "os/os_time.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_distortion_mesh.h"

#include "math/m_api.h"
#include "math/m_mathinclude.h"

#include "r_internal.h"

#include <stdio.h>


/*
 *
 * Functions
 *
 */

static inline struct r_hmd *
r_hmd(struct xrt_device *xdev)
{
	return (struct r_hmd *)xdev;
}

static void
r_hmd_destroy(struct xrt_device *xdev)
{
	struct r_hmd *rh = r_hmd(xdev);

	// Remove the variable tracking.
	u_var_remove_root(rh);

	// Free this device with the helper.
	u_device_free(&rh->base);
}

static void
r_hmd_update_inputs(struct xrt_device *xdev)
{
	struct r_hmd *rh = r_hmd(xdev);
	(void)rh;
}

static void
r_hmd_get_tracked_pose(struct xrt_device *xdev,
                       enum xrt_input_name name,
                       uint64_t at_timestamp_ns,
                       struct xrt_space_relation *out_relation)
{
	struct r_hmd *rh = r_hmd(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		U_LOG_E("Unknown input name");
		return;
	}

	out_relation->pose = rh->r->latest.hmd.pose;
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
	    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);
}

static void
r_hmd_get_hand_tracking(struct xrt_device *xdev,
                        enum xrt_input_name name,
                        uint64_t at_timestamp_ns,
                        struct xrt_hand_joint_set *out_value,
                        uint64_t *out_timestamp_ns)
{
	struct r_hmd *rh = r_hmd(xdev);
	(void)rh;
}

static void
r_hmd_get_view_poses(struct xrt_device *xdev,
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
r_hmd_set_output(struct xrt_device *xdev, enum xrt_output_name name, const union xrt_output_value *value)
{
	// Empty
}

/*!
 * @public @memberof r_hmd
 */
struct xrt_device *
r_hmd_create(struct r_hub *r)
{
	// Allocate.
	const enum u_device_alloc_flags flags = U_DEVICE_ALLOC_HMD;
	const uint32_t input_count = 1;
	const uint32_t output_count = 0;
	struct r_hmd *rh = U_DEVICE_ALLOCATE( //
	    struct r_hmd, flags, input_count, output_count);

	// Setup the basics.
	rh->base.update_inputs = r_hmd_update_inputs;
	rh->base.get_tracked_pose = r_hmd_get_tracked_pose;
	rh->base.get_hand_tracking = r_hmd_get_hand_tracking;
	rh->base.get_view_poses = r_hmd_get_view_poses;
	rh->base.set_output = r_hmd_set_output;
	rh->base.destroy = r_hmd_destroy;
	rh->base.tracking_origin = &r->origin;
	rh->base.orientation_tracking_supported = true;
	rh->base.position_tracking_supported = true;
	rh->base.hand_tracking_supported = false;
	rh->base.name = XRT_DEVICE_GENERIC_HMD;
	rh->base.device_type = XRT_DEVICE_TYPE_HMD;
	rh->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;
	rh->base.inputs[0].active = true;
	rh->r = r;

	// Print name.
	snprintf(rh->base.str, sizeof(rh->base.str), "Remote HMD");
	snprintf(rh->base.serial, sizeof(rh->base.serial), "Remote HMD");

	// Setup info.
	struct u_device_simple_info info;
	info.display.w_pixels = 1920;
	info.display.h_pixels = 1080;
	info.display.w_meters = 0.13f;
	info.display.h_meters = 0.07f;
	info.lens_horizontal_separation_meters = 0.13f / 2.0f;
	info.lens_vertical_position_meters = 0.07f / 2.0f;
	info.fov[0] = 85.0f * (M_PI / 180.0f);
	info.fov[1] = 85.0f * (M_PI / 180.0f);

	if (!u_device_setup_split_side_by_side(&rh->base, &info)) {
		U_LOG_E("Failed to setup basic device info");
		r_hmd_destroy(&rh->base);
		return NULL;
	}

	// Distortion information, fills in xdev->compute_distortion().
	u_distortion_mesh_set_none(&rh->base);

	// Setup variable tracker.
	u_var_add_root(rh, rh->base.str, true);

	return &rh->base;
}
