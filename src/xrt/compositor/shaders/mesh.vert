// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
// Author: Pete Black <pete.black@collabora.com>

#version 450

layout (location = 0)  in vec4 in_pos_ruv;
layout (location = 1)  in vec4 in_guv_buv;

layout (location = 0) out vec2 out_ruv;
layout (location = 1) out vec2 out_guv;
layout (location = 2) out vec2 out_buv;

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

        vec2 pos = 2.0 * (in_pos_ruv.xy - vec2(0.5, 0.5));

        out_ruv = in_pos_ruv.zw;
        out_guv = in_guv_buv.xy;
        out_buv = in_guv_buv.zw;

	// A hack for now.
	if (ubo_vp.viewport_id == 1) {
		pos.x = -pos.x;

                out_ruv.x = 1.0 - out_ruv.x;
                out_guv.x = 1.0 - out_guv.x;
                out_buv.x = 1.0 - out_buv.x;
        }

	if (ubo_vp.flip_y) {
		out_ruv.y = 1.0 - out_ruv.y;
		out_guv.y = 1.0 - out_guv.y;
		out_buv.y = 1.0 - out_buv.y;
	}

	pos = rot * pos;
	gl_Position = vec4(pos, 0.0f, 1.0f);
}
