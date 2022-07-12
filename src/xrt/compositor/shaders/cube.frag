// Copyright 2020 Simon Zeni <simon@bl4ckb0ne.ca>
// Author: Simon Zeni <simon@bl4ckb0ne.ca>
// Author: Bjorn Swenson <bjorn@collabora.com>
// SPDX-License-Identifier: BSL-1.0

#version 460

layout (binding = 0, std140) uniform Transformation
{
	mat4 mvp;
	ivec2 offset;
	ivec2 extent;
	bool flip_y;
} ubo;

layout (binding = 1) uniform samplerCube cube;

layout (location = 0)  in vec2 uv;
layout (location = 0) out vec4 out_color;

void main ()
{
	vec2 frag_coord = vec2(uv) * 2 - 1;
	vec4 view_dir = normalize(ubo.mvp * vec4(frag_coord.x, frag_coord.y, 1, 1));

	out_color = texture(cube, view_dir.xyz);
}
