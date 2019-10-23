// Copyright 2017, James Sarrett.
// Copyright 2017, Bastiaan Olij.
// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: James Sarrett <jsarrett@gmail.com>
// Author: Bastiaan Olij <mux213@gmail.com>
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
#version 450

layout (binding = 0) uniform sampler2D texSampler;

layout (binding = 1, std140) uniform UBO
{
	// Distoriton coefficients (PanoTools model) [a,b,c,d]
	vec4 HmdWarpParam;

	// chromatic distortion post scaling
	vec4 aberr;

	// Position of lens center in m (usually eye_w/2, eye_h/2)
	vec2 LensCenter[2];

	// Scale from texture co-ords to m (usually eye_w, eye_h)
	vec2 ViewportScale;

	// Distortion overall scale in m (usually ~eye_w/2)
	float WarpScale;
} ubo;

layout (location = 0)      in vec2 inUV;
layout (location = 1) flat in int  inViewIndex;

layout (location = 0) out vec4 outColor;


void main()
{
	const int i = inViewIndex;

	vec2 r = inUV * ubo.ViewportScale - ubo.LensCenter[i];

	// scale for distortion model
	// distortion model has r=1 being the largest circle inscribed (e.g. eye_w/2)
	r /= ubo.WarpScale;

	// |r|**2
	float r_mag = length(r);

	// offset for which fragment is sourced
	vec2 r_displaced = r * (
		ubo.HmdWarpParam.w +
		ubo.HmdWarpParam.z * r_mag +
		ubo.HmdWarpParam.y * r_mag * r_mag +
		ubo.HmdWarpParam.x * r_mag * r_mag * r_mag
	);

	// back to world scale
	r_displaced *= ubo.WarpScale;

	// back to viewport co-ord
	vec2 tcR = (ubo.LensCenter[i] + ubo.aberr.r * r_displaced) / ubo.ViewportScale;
	vec2 tcG = (ubo.LensCenter[i] + ubo.aberr.g * r_displaced) / ubo.ViewportScale;
	vec2 tcB = (ubo.LensCenter[i] + ubo.aberr.b * r_displaced) / ubo.ViewportScale;

	vec3 color = vec3(
		texture(texSampler, tcR).r,
		texture(texSampler, tcG).g,
		texture(texSampler, tcB).b
	);

#if 0
	// No need to do this as the texture is clamped to black.
	// Distortion cut-off.
	if (tcG.x < 0.0 || tcG.x > 1.0 || tcG.y < 0.0 || tcG.y > 1.0) {
		color *= 0.125;
	}
#endif

	outColor = vec4(color, 1.0);
}
