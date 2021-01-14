// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Code to handle distortion parameters and fov.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_distortion
 */

#include "xrt/xrt_device.h"

#include "math/m_mathinclude.h"

#include "util/u_misc.h"
#include "util/u_device.h"
#include "util/u_distortion.h"


void
u_distortion_cardboard_calculate(const struct u_cardboard_distortion_arguments *args,
                                 struct xrt_hmd_parts *parts,
                                 struct u_cardboard_distortion *out_dist)
{
	/*
	 * HMD parts
	 */

	uint32_t w_pixels = args->screen.w_pixels / 2;
	uint32_t h_pixels = args->screen.h_pixels;

	// Base assumption, the driver can change afterwards.
	if (parts->blend_mode == 0) {
		parts->blend_mode = XRT_BLEND_MODE_OPAQUE;
	}

	// Use the full screen.
	parts->screens[0].w_pixels = args->screen.w_pixels;
	parts->screens[0].h_pixels = args->screen.h_pixels;

	parts->views[0].viewport.x_pixels = 0;
	parts->views[0].viewport.y_pixels = 0;
	parts->views[0].viewport.w_pixels = w_pixels;
	parts->views[0].viewport.h_pixels = h_pixels;
	parts->views[0].display.w_pixels = w_pixels;
	parts->views[0].display.h_pixels = h_pixels;
	parts->views[0].rot = u_device_rotation_ident;
	parts->views[0].fov = args->fov;

	parts->views[1].viewport.x_pixels = w_pixels;
	parts->views[1].viewport.y_pixels = 0;
	parts->views[1].viewport.w_pixels = w_pixels;
	parts->views[1].viewport.h_pixels = h_pixels;
	parts->views[1].display.w_pixels = w_pixels;
	parts->views[1].display.h_pixels = h_pixels;
	parts->views[1].rot = u_device_rotation_ident;
	parts->views[1].fov = args->fov;


	/*
	 * Left values
	 */

	// clang-format off
	struct u_cardboard_distortion_values l_values = {0};
	l_values.distortion_k[0] = args->distortion_k[0];
	l_values.distortion_k[1] = args->distortion_k[1];
	l_values.distortion_k[2] = args->distortion_k[2];
	l_values.distortion_k[3] = args->distortion_k[3];
	l_values.distortion_k[4] = args->distortion_k[4];
	l_values.screen.size.x = args->screen.w_meters;
	l_values.screen.size.y = args->screen.h_meters;
	l_values.screen.offset.x = (args->screen.w_meters - args->inter_lens_distance_meters) / 2.0;
	l_values.screen.offset.y = args->lens_y_center_on_screen_meters;
	// clang-format on

	// Turn into tanangles
	l_values.screen.size.x /= args->screen_to_lens_distance_meters;
	l_values.screen.size.y /= args->screen_to_lens_distance_meters;
	l_values.screen.offset.x /= args->screen_to_lens_distance_meters;
	l_values.screen.offset.y /= args->screen_to_lens_distance_meters;

	// Tan-angle to texture coordinates
	// clang-format off
	l_values.texture.size.x = tan(-args->fov.angle_left) + tan(args->fov.angle_right);
	l_values.texture.size.y = tan(args->fov.angle_up) + tan(-args->fov.angle_down);
	l_values.texture.offset.x = tan(-args->fov.angle_left);
	l_values.texture.offset.y = tan(-args->fov.angle_down);
	// clang-format on

	// Fix up views not covering the entire screen.
	l_values.screen.size.x /= 2.0;


	/*
	 * Right values
	 */

	// clang-format off
	struct u_cardboard_distortion_values r_values = {0};
	r_values.distortion_k[0] = args->distortion_k[0];
	r_values.distortion_k[1] = args->distortion_k[1];
	r_values.distortion_k[2] = args->distortion_k[2];
	r_values.distortion_k[3] = args->distortion_k[3];
	r_values.distortion_k[4] = args->distortion_k[4];
	r_values.screen.size.x = args->screen.w_meters;
	r_values.screen.size.y = args->screen.h_meters;
	r_values.screen.offset.x = (args->screen.w_meters + args->inter_lens_distance_meters) / 2.0;
	r_values.screen.offset.y = args->lens_y_center_on_screen_meters;
	// clang-format on

	// Turn into tanangles
	r_values.screen.size.x /= args->screen_to_lens_distance_meters;
	r_values.screen.size.y /= args->screen_to_lens_distance_meters;
	r_values.screen.offset.x /= args->screen_to_lens_distance_meters;
	r_values.screen.offset.y /= args->screen_to_lens_distance_meters;

	// Tanangle to texture coordinates
	// clang-format off
	r_values.texture.size.x = tan(-args->fov.angle_left) + tan(args->fov.angle_right);
	r_values.texture.size.y = tan(args->fov.angle_up) + tan(-args->fov.angle_down);
	r_values.texture.offset.x = tan(-args->fov.angle_left);
	r_values.texture.offset.y = tan(-args->fov.angle_down);
	// clang-format on

	// Fix up views not covering the entire screen.
	r_values.screen.size.x /= 2.0;
	r_values.screen.offset.x -= r_values.screen.size.x;


	/*
	 * Write results.
	 */

	// Copy the arguments.
	out_dist->args = *args;

	// Save the results.
	out_dist->values[0] = l_values;
	out_dist->values[1] = r_values;
}
