// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
// Author: Pete Black <pete.black@collabora.com>
// Author: Jakob Bornecrantz <jakob@collabora.com>

#version 450


layout (binding = 1, std140) uniform Config
{
	vec4 vertex_rot;
	vec4 post_transform;
} ubo;

layout (location = 0)  in vec4 in_pos_ruv;
layout (location = 1)  in vec4 in_guv_buv;
layout (location = 0) out vec2 out_r_uv;
layout (location = 1) out vec2 out_g_uv;
layout (location = 2) out vec2 out_b_uv;

out gl_PerVertex
{
	vec4 gl_Position;
};


vec2 transform_uv(vec2 uv)
{
	vec2 values = uv;

	// To deal with OpenGL flip and sub image view.
	values.xy = fma(values.xy, ubo.post_transform.zw, ubo.post_transform.xy);

	// Ready to be used.
	return values.xy;
}

void main()
{
	mat2x2 rot = {
		ubo.vertex_rot.xy,
		ubo.vertex_rot.zw,
	};

	vec2 pos = rot * in_pos_ruv.xy;
	gl_Position = vec4(pos, 0.0f, 1.0f);

	vec2 r_uv = in_pos_ruv.zw;
	vec2 g_uv = in_guv_buv.xy;
	vec2 b_uv = in_guv_buv.zw;

	r_uv = transform_uv(r_uv);
	g_uv = transform_uv(g_uv);
	b_uv = transform_uv(b_uv);

	out_r_uv = r_uv;
	out_g_uv = g_uv;
	out_b_uv = b_uv;
}
