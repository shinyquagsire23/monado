// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Jakob Bornecrantz <jakob@collabora.com>

#version 450

layout (binding = 0) uniform sampler2D tex_sampler;

layout (location = 0)      in vec2 in_uv;
layout (location = 1) flat in  int in_view_index;
layout (location = 0)     out vec4 out_color;


void main()
{
	out_color = vec4(texture(tex_sampler, in_uv).rgb, 1.0);
}
