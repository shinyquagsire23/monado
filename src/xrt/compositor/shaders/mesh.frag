// Copyright 2017, James Sarrett.
// Copyright 2017, Bastiaan Olij.
// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: James Sarrett <jsarrett@gmail.com>
// Author: Bastiaan Olij <mux213@gmail.com>
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
// Author: Pete Black <pete.black@collabora.com>

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

layout (location = 0)  in vec2 in_ruv;
layout (location = 1)  in vec2 in_guv;
layout (location = 2)  in vec2 in_buv;
layout (location = 0) out vec4 out_color;


void main()
{
	float r = texture(tex_sampler, in_ruv).x;
	float g = texture(tex_sampler, in_guv).y;
	float b = texture(tex_sampler, in_buv).z;

        out_color = vec4(r, g, b, 1.0);
}
