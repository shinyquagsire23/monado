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
 *
 *
 * @ingroup aux_util
 */
struct u_uv_generator
{
	void (*calc)(struct u_uv_generator *,
	             int view,
	             float u,
	             float v,
	             float result[6]);

	void (*destroy)(struct u_uv_generator *);
};

/*!
 * Given a callback and a user_ptr generates num_views meshes.
 *
 * @ingroup aux_util
 */
void
u_distortion_mesh_from_gen(struct u_uv_generator *,
                           int num_views,
                           struct xrt_hmd_parts *target);

/*!
 * Given two sets of panotools values creates a mesh generator, copies the
 * values into it. This probably isn't the function you want.
 *
 * @ingroup aux_util
 */
void
u_distortion_mesh_generator_from_panotools(
    const struct u_panotools_values *left,
    const struct u_panotools_values *right,
    struct u_uv_generator **out_gen);

/*!
 * Given two sets of panotools values creates the left and th right uv meshes.
 * This is probably the function you want.
 *
 * @ingroup aux_util
 */
void
u_distortion_mesh_from_panotools(const struct u_panotools_values *left,
                                 const struct u_panotools_values *right,
                                 struct xrt_hmd_parts *target);

/*!
 * Create two distortion meshes with no distortion.
 *
 * @ingroup aux_util
 */
void
u_distortion_mesh_none(struct xrt_hmd_parts *target);


#ifdef __cplusplus
}
#endif
