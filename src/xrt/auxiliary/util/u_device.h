// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Misc helpers for device drivers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_tracking.h"

#ifdef __cplusplus
extern "C" {
#endif


extern const struct xrt_matrix_2x2 u_device_rotation_right;
extern const struct xrt_matrix_2x2 u_device_rotation_left;
extern const struct xrt_matrix_2x2 u_device_rotation_ident;
extern const struct xrt_matrix_2x2 u_device_rotation_180;

enum u_device_alloc_flags
{
	// clang-format off
	U_DEVICE_ALLOC_NO_FLAGS      = 0,
	U_DEVICE_ALLOC_HMD           = 1 << 0,
	U_DEVICE_ALLOC_TRACKING_NONE = 1 << 1,
	// clang-format on
};

struct u_device_simple_info
{
	struct
	{
		uint32_t w_pixels;
		uint32_t h_pixels;
		float w_meters;
		float h_meters;
	} display;

	float lens_horizontal_separation_meters;
	float lens_vertical_position_meters;

	struct
	{
		float fov;
	} views[2];
};

/*!
 * Setup the device information given a very simple info struct.
 *
 * @return true on success.
 * @ingroup aux_util
 */
bool
u_device_setup_split_side_by_side(struct xrt_device *xdev, const struct u_device_simple_info *info);

/*!
 * Dump the device config to stderr.
 *
 * @ingroup aux_util
 */
void
u_device_dump_config(struct xrt_device *xdev, const char *prefix, const char *prod);

#define U_DEVICE_ALLOCATE(type, flags, num_inputs, num_outputs)                                                        \
	((type *)u_device_allocate(flags, sizeof(type), num_inputs, num_outputs))


/*!
 * Helper function to allocate a device plus inputs in the same allocation
 * placed after the device in memory.
 *
 * Will setup any pointers and num values.
 *
 * @ingroup aux_util
 */
void *
u_device_allocate(enum u_device_alloc_flags flags, size_t size, size_t num_inputs, size_t num_outputs);

/*!
 * Helper function to free a device and any data hanging of it.
 *
 * @ingroup aux_util
 */
void
u_device_free(struct xrt_device *xdev);


#define XRT_DEVICE_ROLE_UNASSIGNED (-1)

/*!
 * Helper function to assign head, left hand and right hand roles.
 *
 * @ingroup aux_util
 */
void
u_device_assign_xdev_roles(struct xrt_device **xdevs, size_t num_xdevs, int *head, int *left, int *right);

/*!
 * Helper function to assign head, left hand and right hand roles.
 *
 * @ingroup aux_util
 */
void
u_device_setup_tracking_origins(struct xrt_device *head,
                                struct xrt_device *left,
                                struct xrt_device *right,
                                struct xrt_vec3 *global_tracking_origin_offset);

#ifdef __cplusplus
}
#endif
