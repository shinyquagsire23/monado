// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
// Author: Pete Black <pete.black@collabora.com>
// Author: Jakob Bornecrantz <jakob@collabora.com>

#version 450

layout (binding = 1, std140) uniform ubo
{
	vec4 vertex_rot;
} ubo_vp;

layout (location = 0)  in vec4 in_pos_ruv;
layout (location = 1)  in vec4 in_guv_buv;
layout (location = 0) out vec2 out_ruv;
layout (location = 1) out vec2 out_guv;
layout (location = 2) out vec2 out_buv;

out gl_PerVertex
{
	vec4 gl_Position;
};


void main()
{
	mat2x2 rot = {
		ubo_vp.vertex_rot.xy,
		ubo_vp.vertex_rot.zw,
	};

	vec2 pos = rot * in_pos_ruv.xy;
	out_ruv = in_pos_ruv.zw;
	out_guv = in_guv_buv.xy;
	out_buv = in_guv_buv.zw;

	gl_Position = vec4(pos, 0.0f, 1.0f);
}
