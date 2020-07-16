// Copyright 2020 Collabora Ltd.
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
// SPDX-License-Identifier: BSL-1.0

#version 460

layout (location = 0) in vec2 uv;

layout (binding = 0, std140) uniform Transformation {
  mat4 mvp;
  ivec2 offset;
  ivec2 extent;
  bool flip_y;
} ubo;
layout (binding = 1) uniform sampler2D image;

layout (location = 0) out vec4 out_color;

void main ()
{
  vec2 uv_sub = vec2(ubo.offset) + uv * vec2(ubo.extent);
  uv_sub /= textureSize(image, 0);
  out_color = texture(image, uv_sub);
}

