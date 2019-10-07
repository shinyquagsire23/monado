// Copyright 2017, James Sarrett.
// Copyright 2017, Bastiaan Olij.
// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: James Sarrett <jsarrett@gmail.com>
// Author: Bastiaan Olij <mux213@gmail.com>
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
// Author: Pete Black <pete.black@collabora.com>

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

layout (location = 0)  in vec2 inUV;
layout (location = 0) out vec4 outColor;

void main()
{
	vec3 color = texture(texSampler, inUV).xyz;
	outColor = vec4(color,1.0);
}
