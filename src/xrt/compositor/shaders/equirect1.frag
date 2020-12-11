// Copyright 2020 Collabora Ltd.
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
// SPDX-License-Identifier: BSL-1.0

#version 460

layout (location = 0) in vec2 uv;

layout (set = 0, binding = 0, std140) uniform Transformation {
  mat4 mvp;
  ivec2 offset;
  ivec2 extent;
  bool flip_y;
} ubo;

layout (set = 0, binding = 1) uniform sampler2D image;

layout (set = 1, binding = 0, std140) uniform Equirect {
  vec2 scale;
  vec2 bias;
  float radius;
} equirect;

layout (location = 0) out vec4 out_color;

const float PI = 3.1416;

void main ()
{
  vec2 uv_sub = vec2(ubo.offset) + uv * vec2(ubo.extent);
  uv_sub /= textureSize(image, 0);

  vec2 frag_coord = vec2(uv_sub) * 2 - 1;

  vec4 view_dir = normalize(ubo.mvp * vec4(frag_coord.x, -frag_coord.y, 1, 1));

  float lat = atan(view_dir.x, -view_dir.z) / (2 * PI);
  float lon = acos(view_dir.y) / PI;

  lat *= equirect.scale.x;
  lon *= equirect.scale.y;

  lat += equirect.bias.x;
  lon += equirect.bias.y;

  out_color = texture(image, vec2(lat, lon));
}

