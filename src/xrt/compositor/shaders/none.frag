// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Jakob Bornecrantz <jakob@collabora.com>
#version 450

layout (binding = 0) uniform sampler2D texSampler;

layout (location = 0)      in vec2 inUV;
layout (location = 1) flat in int  inViewIndex;

layout (location = 0) out vec4 outColor;


void main()
{
	outColor = vec4(texture(texSampler, inUV).rgb, 1.0);
}
