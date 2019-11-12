// Copyright      2017, Philipp Zabel.
// Copyright 2017-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Philipp Zabel <philipp.zabel@gmail.com>
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>

#version 450

layout (binding = 0) uniform sampler2D tex_sampler;

layout (binding = 1, std140) uniform UBO
{
	vec4 coeffs[2][3];
	vec4 center[2];
	vec4 undistort_r2_cutoff;
	float aspect_x_over_y;
	float grow_for_undistort;
} ubo;

layout (location = 0)      in vec2 in_uv;
layout (location = 1) flat in  int in_view_index;
layout (location = 0)     out vec4 out_color;


void main()
{
	const int i = in_view_index;

	const vec2 factor = 0.5 / (1.0 + ubo.grow_for_undistort)
	                    * vec2(1.0, ubo.aspect_x_over_y);

	vec2 texCoord = 2.0 * in_uv - vec2(1.0);

	texCoord.y /= ubo.aspect_x_over_y;
	texCoord -= ubo.center[i].xy;

	float r2 = dot(texCoord, texCoord);

	vec3 d_inv = ((r2 * ubo.coeffs[i][2].xyz + ubo.coeffs[i][1].xyz)
	             * r2 + ubo.coeffs[i][0].xyz)
	             * r2 + vec3(1.0);

	const vec3 d = 1.0 / d_inv;

	const vec2 offset = vec2(0.5);

	vec2 tc_r = offset + (texCoord * d.r + ubo.center[i].xy) * factor;
	vec2 tc_g = offset + (texCoord * d.g + ubo.center[i].xy) * factor;
	vec2 tc_b = offset + (texCoord * d.b + ubo.center[i].xy) * factor;

	vec3 color = vec3(
	      texture(tex_sampler, tc_r).r,
	      texture(tex_sampler, tc_g).g,
	      texture(tex_sampler, tc_b).b);

#if 0
	if (r2 > ubo.undistort_r2_cutoff[i]) {
		color *= 0.125;
	}
#endif

	out_color = vec4(color, 1.0);
}
