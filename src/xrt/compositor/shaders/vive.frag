// Copyright      2017, Philipp Zabel.
// Copyright 2017-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Philipp Zabel <philipp.zabel@gmail.com>
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
#version 450

layout (binding = 0) uniform sampler2D texSampler;

layout (binding = 1, std140) uniform UBO
{
	vec4 coeffs[2][3];
	vec4 center[2];
	vec4 undistort_r2_cutoff;
	float aspect_x_over_y;
	float grow_for_undistort;
} ubo;

layout (location = 0) in vec2 inUV;
layout (location = 1) flat in int inViewIndex;

layout (location = 0) out vec4 outColor;


void main()
{
	const int i = inViewIndex;

	const vec2 factor = 0.5 / (1.0 + ubo.grow_for_undistort)
	                    * vec2(1.0, ubo.aspect_x_over_y);

	vec2 texCoord = 2.0 * inUV - vec2(1.0);

	texCoord.y /= ubo.aspect_x_over_y;
	texCoord -= ubo.center[i].xy;

	float r2 = dot(texCoord, texCoord);

	vec3 d_inv = ((r2 * ubo.coeffs[i][2].xyz + ubo.coeffs[i][1].xyz)
	             * r2 + ubo.coeffs[i][0].xyz)
	             * r2 + vec3(1.0);

	const vec3 d = 1.0 / d_inv;

	const vec2 offset = vec2(0.5);

	vec2 tcR = offset + (texCoord * d.r + ubo.center[i].xy) * factor;
	vec2 tcG = offset + (texCoord * d.g + ubo.center[i].xy) * factor;
	vec2 tcB = offset + (texCoord * d.b + ubo.center[i].xy) * factor;

	vec3 color = vec3(
	      texture(texSampler, tcR).r,
	      texture(texSampler, tcG).g,
	      texture(texSampler, tcB).b);

	if (r2 > ubo.undistort_r2_cutoff[i]) {
		color *= 0.125;
	}

	outColor = vec4(color, 1.0);
}
