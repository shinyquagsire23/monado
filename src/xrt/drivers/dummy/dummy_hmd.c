// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Dummy HMD device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_dummy
 */


#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "os/os_time.h"
#include "math/m_api.h"
#include "xrt/xrt_device.h"
#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_time.h"
#include "util/u_distortion_mesh.h"


/*
 *
 * Structs and defines.
 *
 */

/*!
 * @implements xrt_device
 */
struct dummy_hmd
{
	struct xrt_device base;

	struct xrt_pose pose;

	bool print_spew;
	bool print_debug;
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

DEBUG_GET_ONCE_BOOL_OPTION(dummy_spew, "DUMMY_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(dummy_debug, "DUMMY_PRINT_DEBUG", false)

#define DH_SPEW(dh, ...)                                                       \
	do {                                                                   \
		if (dh->print_spew) {                                          \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define DH_DEBUG(dh, ...)                                                      \
	do {                                                                   \
		if (dh->print_debug) {                                         \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

#define DH_ERROR(dh, ...)                                                      \
	do {                                                                   \
		fprintf(stderr, "%s - ", __func__);                            \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, "\n");                                         \
	} while (false)

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
	// Empty
}

static void
dummy_hmd_get_tracked_pose(struct xrt_device *xdev,
                           enum xrt_input_name name,
                           uint64_t at_timestamp_ns,
                           uint64_t *out_relation_timestamp_ns,
                           struct xrt_space_relation *out_relation)
{
	struct dummy_hmd *dh = dummy_hmd(xdev);

	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		DH_ERROR(dh, "unknown input name");
		return;
	}

	uint64_t now = os_monotonic_get_ns();

	*out_relation_timestamp_ns = now;
	out_relation->pose = dh->pose;
	out_relation->relation_flags = (enum xrt_space_relation_flags)(
	    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
	    XRT_SPACE_RELATION_POSITION_VALID_BIT);
}

static void
dummy_hmd_get_view_pose(struct xrt_device *xdev,
                        struct xrt_vec3 *eye_relation,
                        uint32_t view_index,
                        struct xrt_pose *out_pose)
{
	struct xrt_pose pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
	bool adjust = view_index == 0;

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

struct xrt_device *
dummy_hmd_create(void)
{
	enum u_device_alloc_flags flags = (enum u_device_alloc_flags)(
	    U_DEVICE_ALLOC_HMD | U_DEVICE_ALLOC_TRACKING_NONE);
	struct dummy_hmd *dh = U_DEVICE_ALLOCATE(struct dummy_hmd, flags, 1, 0);
	dh->base.update_inputs = dummy_hmd_update_inputs;
	dh->base.get_tracked_pose = dummy_hmd_get_tracked_pose;
	dh->base.get_view_pose = dummy_hmd_get_view_pose;
	dh->base.destroy = dummy_hmd_destroy;
	dh->base.name = XRT_DEVICE_GENERIC_HMD;
	dh->pose.orientation.w = 1.0f; // All other values set to zero.
	dh->print_spew = debug_get_bool_option_dummy_spew();
	dh->print_debug = debug_get_bool_option_dummy_debug();

	// Print name.
	snprintf(dh->base.str, XRT_DEVICE_NAME_LEN, "Dummy HMD");

	// Setup input.
	dh->base.inputs[0].name = XRT_INPUT_GENERIC_HEAD_POSE;

	// Setup info.
	struct u_device_simple_info info;
	info.display.w_pixels = 1920;
	info.display.h_pixels = 1080;
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

	// Setup variable tracker.
	u_var_add_root(dh, "Dummy HMD", true);
	u_var_add_pose(dh, &dh->pose, "pose");

	if (dh->base.hmd->distortion.preferred == XRT_DISTORTION_MODEL_NONE) {
		// Setup the distortion mesh.
		u_distortion_mesh_none(dh->base.hmd);
	}
	dh->base.device_type = XRT_DEVICE_TYPE_HMD;

	return &dh->base;
}
