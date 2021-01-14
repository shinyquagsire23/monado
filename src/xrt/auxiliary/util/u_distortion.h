// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to handle distortion parameters and fov.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_distortion
 */

#pragma once

#include "xrt/xrt_defines.h"


#ifdef __cplusplus
extern "C" {
#endif


struct xrt_hmd_parts;

/*!
 * @defgroup aux_distortion Distortion utilities.
 * @ingroup aux_util
 */

/*!
 * These are the values that you need to supply to the distortion code to setup
 * a @ref u_cardboard_distortion properly.
 *
 * @ingroup aux_distortion
 */
struct u_cardboard_distortion_arguments
{
	float distortion_k[5];

	struct
	{
		uint32_t w_pixels, h_pixels;
		float w_meters, h_meters;
	} screen;

	//! Distances between the lenses in meters.
	float inter_lens_distance_meters;

	//! Where on the y axis the center of the lens is on the screen.
	float lens_y_center_on_screen_meters;

	/*!
	 * The distance to the lens from the screen, used to calculate calculate
	 * tanangle of various distances on the screen.
	 */
	float screen_to_lens_distance_meters;

	//! Fov values that the cardboard configuration has given us.
	struct xrt_fov fov;
};

/*!
 * Values to create a distortion mesh from cardboard values.
 *
 * This matches the formula in the cardboard SDK, while the array is fixed size
 * just setting the K value to zero will make it not have a effect.
 *
 *    p' = p (1 + K0 r^2 + K1 r^4 + ... + Kn r^(2n))
 *
 * @ingroup aux_distortion
 */
struct u_cardboard_distortion_values
{
	//! Cardboard distortion k values.
	float distortion_k[5];

	struct
	{
		//! Used to transform to and from tanangle space.
		struct xrt_vec2 size;
		//! Used to transform to and from tanangle space.
		struct xrt_vec2 offset;
	} screen, texture;
};

/*!
 * Both given and derived values needed for cardboard distortion.
 *
 * @ingroup aux_distortion
 */
struct u_cardboard_distortion
{
	//! Arguments this distortion was created from.
	struct u_cardboard_distortion_arguments args;

	//! Distortion parameters, some derived from @ref args.
	struct u_cardboard_distortion_values values[2];
};

/*!
 * Take cardboard arguments to turn them into a @ref u_cardboard_distortion and
 * fill out a @ref xrt_hmd_parts struct.
 *
 * @ingroup aux_distortion
 */
void
u_distortion_cardboard_calculate(const struct u_cardboard_distortion_arguments *args,
                                 struct xrt_hmd_parts *parts,
                                 struct u_cardboard_distortion *out_dist);


#ifdef __cplusplus
}
#endif
