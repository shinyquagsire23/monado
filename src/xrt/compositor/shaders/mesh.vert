// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
// Author: Pete Black <pete.black@collabora.com>

#version 450

layout (location = 0)  in vec4 pos_uv;
layout (location = 0) out vec2 out_uv;

layout (binding = 2, std140) uniform ubo
{
	vec4 rot;
	int viewport_id;
	bool flip_y;
} ubo_vp;

out gl_PerVertex
{
	vec4 gl_Position;
};


void main()
{
	mat2x2 rot = {
		ubo_vp.rot.xy,
		ubo_vp.rot.zw,
	};

	out_uv = pos_uv.zw;

	vec2 pos = 2.0 * (pos_uv.xy - vec2(0.5, 0.5));

	// A hack for now.
	if (ubo_vp.viewport_id == 1) {
		pos.x = -pos.x;
		out_uv.x = 1.0 - pos_uv.z;
	}

	pos = rot * pos;
	gl_Position = vec4(pos, 0.0f, 1.0f);

	if (ubo_vp.flip_y) {
		out_uv.y = 1.0 - out_uv.y;
	}
}
