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
} transformation;

layout (location = 0)  in vec3 position;
layout (location = 1)  in vec2 uv;
layout (location = 0) out vec2 out_uv;

const mat4 mvp = mat4(
	2, 0, 0, 0,
	0, 2, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1
);

void main() {
	gl_Position = mvp * vec4(position, 1.0);
	gl_Position.y *= -1.;

	out_uv = uv;

	if (transformation.flip_y) {
		out_uv.y = 1.0 - out_uv.y;
	}
}
