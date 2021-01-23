// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to generate disortion meshes.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_distortion
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_defines.h"

#include "util/u_distortion.h"


#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Panotools distortion
 *
 */

/*!
 * Values to create a distortion mesh from panotools values.
 *
 * @ingroup aux_distortion
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
 * Distortion correction implementation for Panotools distortion values.
 *
 * @ingroup aux_distortion
 */
bool
u_compute_distortion_panotools(struct u_panotools_values *values, float u, float v, struct xrt_uv_triplet *result);


/*
 *
 * Vive, Vive Pro & Index distortion
 *
 */

/*!
 * Values to create a distortion mesh from Vive configuration values.
 *
 * @ingroup aux_distortion
 */
struct u_vive_values
{
	float aspect_x_over_y;
	float grow_for_undistort;

	float undistort_r2_cutoff;

	//! r/g/b
	struct xrt_vec2 center[3];

	//! r/g/b, a/b/c/d
	float coefficients[3][4];
};

/*!
 * Distortion correction implementation for the Vive, Vive Pro, Valve Index
 * distortion values found in the HMD configuration.
 *
 * @ingroup aux_distortion
 */
bool
u_compute_distortion_vive(struct u_vive_values *values, float u, float v, struct xrt_uv_triplet *result);


/*
 *
 * Cardboard mesh distortion parameters.
 *
 */

/*!
 * Distortion correction implementation for the Cardboard devices.
 *
 * @ingroup aux_distortion
 */
bool
u_compute_distortion_cardboard(struct u_cardboard_distortion_values *values,
                               float u,
                               float v,
                               struct xrt_uv_triplet *result);


/*
 *
 * None distortion
 *
 */

/*!
 * Identity distortion correction sets all result coordinates to u,v.
 *
 * @ingroup aux_distortion
 */
bool
u_compute_distortion_none(float u, float v, struct xrt_uv_triplet *result);

/*!
 * Helper function for none distortion devices.
 *
 * @ingroup aux_distortion
 */
bool
u_distortion_mesh_none(struct xrt_device *xdev, int view, float u, float v, struct xrt_uv_triplet *result);


/*
 *
 * Mesh generation functions.
 *
 */

/*!
 * Given a @ref xrt_device generates meshes by calling
 * xdev->compute_distortion(), populates `xdev->hmd_parts.distortion.mesh` &
 * `xdev->hmd_parts.distortion.models`.
 *
 * @relatesalso xrt_device
 * @ingroup aux_distortion
 */
void
u_distortion_mesh_fill_in_compute(struct xrt_device *xdev);

/*!
 * Given a @ref xrt_device generates a no distortion mesh, populates
 * `xdev->hmd_parts.distortion.mesh` & `xdev->hmd_parts.distortion.models`.
 *
 * @relatesalso xrt_device
 * @ingroup aux_distortion
 */
void
u_distortion_mesh_fill_in_none(struct xrt_device *xdev);

/*!
 * Given a @ref xrt_device generates a no distortion mesh, also sets
 * `xdev->compute_distortion()` and populates `xdev->hmd_parts.distortion.mesh`
 * & `xdev->hmd_parts.distortion.models`.
 *
 * @relatesalso xrt_device
 * @ingroup aux_distortion
 */
void
u_distortion_mesh_set_none(struct xrt_device *xdev);


#ifdef __cplusplus
}
#endif
