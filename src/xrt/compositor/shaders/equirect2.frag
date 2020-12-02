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
  float radius;
  float central_horizontal_angle;
  float upper_vertical_angle;
  float lower_vertical_angle;
} equirect;

layout (location = 0) out vec4 out_color;

const float PI = 3.1416;

// #define DEBUG 1

void main ()
{
  vec2 uv_sub = vec2(ubo.offset) + uv * vec2(ubo.extent);
  uv_sub /= textureSize(image, 0);

  vec2 frag_coord = vec2(uv_sub) * 2 - 1;

  vec4 view_dir = normalize(ubo.mvp * vec4(frag_coord.x, -frag_coord.y, 1, 1));

  float lat = atan(view_dir.x, -view_dir.z) / (2 * PI);
  float lon = acos(view_dir.y) / PI;

#ifdef DEBUG
  int lat_int = int(lat * 1000.0);
  int lon_int = int(lon * 1000.0);

  if (lat < 0.001 && lat > -0.001)
    out_color = vec4(1, 0, 0, 1);
  else if (lat_int % 50 == 0)
    out_color = vec4(1, 1, 1, 1);
  else if (lon_int % 50 == 0)
    out_color = vec4(1, 1, 1, 1);
  else
    out_color = vec4(lat, lon, 0, 1);
#endif

  float chan = equirect.central_horizontal_angle / (PI * 2.0f);

  // Normalize [0, 2π] to [0, 1]
  float uhan = chan / 2.0f;
  float lhan = -chan / 2.0f;

  // Normalize [-π/2, π/2] to [0, 1]
  float uvan = equirect.upper_vertical_angle / PI + 0.5f;
  float lvan = equirect.lower_vertical_angle / PI + 0.5f;

  if (lon < uvan && lon > lvan && lat < uhan && lat > lhan)
#ifdef DEBUG
    out_color += texture(image, vec2(lat, lon)) / 2.0;
#else
    out_color = texture(image, vec2(lat, lon));
  else
    out_color = vec4(0, 0, 0, 0);
#endif
}

