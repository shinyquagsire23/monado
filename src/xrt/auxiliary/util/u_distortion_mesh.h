// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to generate disortion meshes.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Values to create a distortion mesh from panotools values.
 *
 * @ingroup aux_util
 */
struct u_panotools_values
{
	//! Panotools universal distortion k (reverse order from OpenHMD).
	float distortion_k[5];
	//! Panotools post distortion scale, <r, g, b>.
	float aberration_k[3];
	//! Panotools warp scale.
	float scale;
	//! Center of the lens.
	struct xrt_vec2 lens_center;
	//! Viewport size.
	struct xrt_vec2 viewport_size;
};

/*!
 * Values to create a distortion mesh from Vive configuration values.
 *
 * @ingroup aux_util
 */
struct u_vive_values
{
	float aspect_x_over_y;
	float grow_for_undistort;

	//! Left/right
	float undistort_r2_cutoff;

	//! Left/right, x/y
	float center[2];

	//! left/right, r/g/b, a/b/c
	float coefficients[3][3];
};

/*!
 * Distortion correction implementation for Panotools distortion values.
 *
 * @ingroup aux_util
 */
bool
u_compute_distortion_panotools(struct u_panotools_values *values,
                               float u,
                               float v,
                               struct xrt_uv_triplet *result);

/*!
 * Distortion correction implementation for the Vive, Vive Pro, Valve Index
 * distortion values found in the HMD configuration.
 *
 * @ingroup aux_util
 */
bool
u_compute_distortion_vive(struct u_vive_values *values,
                          float u,
                          float v,
                          struct xrt_uv_triplet *result);

/*!
 * Identity distortion correction sets all result coordinates to u,v.
 *
 * @ingroup aux_util
 */
bool
u_compute_distortion_none(float u, float v, struct xrt_uv_triplet *result);

/*!
 * Helper function for none distortion devices.
 *
 * @ingroup aux_util
 */
bool
u_distortion_mesh_none(struct xrt_device *xdev,
                       int view,
                       float u,
                       float v,
                       struct xrt_uv_triplet *result);


/*!
 * Given a @ref xrt_device generates meshes by calling
 * xdev->compute_distortion(), populates `xdev->hmd_parts.distortion.mesh` &
 * `xdev->hmd_parts.distortion.models`.
 *
 * @ingroup aux_util
 * @relatesalso xrt_device
 */
void
u_distortion_mesh_fill_in_compute(struct xrt_device *xdev);

/*!
 * Given a @ref xrt_device generates a no distortion mesh, populates
 * `xdev->hmd_parts.distortion.mesh` & `xdev->hmd_parts.distortion.models`.
 *
 * @ingroup aux_util
 * @relatesalso xrt_device
 */
void
u_distortion_mesh_fill_in_none(struct xrt_device *xdev);

/*!
 * Given a @ref xrt_device generates a no distortion mesh, also sets
 * `xdev->compute_distortion()` and populates `xdev->hmd_parts.distortion.mesh`
 * & `xdev->hmd_parts.distortion.models`.
 *
 * @ingroup aux_util
 * @relatesalso xrt_device
 */
void
u_distortion_mesh_set_none(struct xrt_device *xdev);


#ifdef __cplusplus
}
#endif
