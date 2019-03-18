// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
#version 450

layout (location = 0) out vec2 outUV;
layout (location = 1) out int  outViewIndex;

layout (binding = 2, std140) uniform UBO
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

	outViewIndex = ubo_vp.viewport_id;
	outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(rot * (outUV * 2.0f - 1.0f), 0.0f, 1.0f);
	if (ubo_vp.flip_y)
		outUV.y = 1.0 - outUV.y;
}
