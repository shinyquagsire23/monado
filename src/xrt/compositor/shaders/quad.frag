// Copyright 2020 Collabora Ltd.
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
// SPDX-License-Identifier: BSL-1.0

#version 460

layout (location = 0) in vec2 uv;

layout (binding = 1) uniform sampler2D image;

layout (location = 0) out vec4 out_color;

void main ()
{
  vec4 texture_color = texture (image, uv);
  out_color = texture_color;
}

