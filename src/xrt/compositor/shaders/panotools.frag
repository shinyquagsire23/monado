// Copyright 2017, James Sarrett.
// Copyright 2017, Bastiaan Olij.
// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: James Sarrett <jsarrett@gmail.com>
// Author: Bastiaan Olij <mux213@gmail.com>
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>

#version 450

layout (binding = 0) uniform sampler2D tex_sampler;

layout (binding = 1, std140) uniform UBO
{
	// Distoriton coefficients (PanoTools model) [a,b,c,d]
	vec4 hmd_warp_param;

	// chromatic distortion post scaling
	vec4 aberr;

	// Position of lens center in m (usually eye_w/2, eye_h/2)
	vec2 lens_center[2];

	// Scale from texture co-ords to m (usually eye_w, eye_h)
	vec2 viewport_scale;

	// Distortion overall scale in m (usually ~eye_w/2)
	float warp_scale;
} ubo;

layout (location = 0)      in vec2 in_uv;
layout (location = 1) flat in int  in_view_index;
layout (location = 0) out vec4 out_color;


void main()
{
	const int i = in_view_index;

	vec2 r = in_uv * ubo.viewport_scale - ubo.lens_center[i];

	// scale for distortion model
	// distortion model has r=1 being the largest circle inscribed (e.g. eye_w/2)
	r /= ubo.warp_scale;

	// |r|**2
	float r_mag = length(r);

	// offset for which fragment is sourced
	vec2 r_displaced = r * (
		ubo.hmd_warp_param.w +
		ubo.hmd_warp_param.z * r_mag +
		ubo.hmd_warp_param.y * r_mag * r_mag +
		ubo.hmd_warp_param.x * r_mag * r_mag * r_mag
	);

	// back to world scale
	r_displaced *= ubo.warp_scale;

	// back to viewport co-ord
	vec2 tc_t = (ubo.lens_center[i] + ubo.aberr.r * r_displaced) / ubo.viewport_scale;
	vec2 tc_g = (ubo.lens_center[i] + ubo.aberr.g * r_displaced) / ubo.viewport_scale;
	vec2 tc_b = (ubo.lens_center[i] + ubo.aberr.b * r_displaced) / ubo.viewport_scale;

	vec3 color = vec3(
		texture(tex_sampler, tc_t).r,
		texture(tex_sampler, tc_g).g,
		texture(tex_sampler, tc_b).b
	);

	out_color = vec4(color, 1.0);
}
