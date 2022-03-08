# Copyright 2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


# Old RGB hand tracking library.
lib_t_ht_old_rgb = static_library(
	't_ht_old_rgb',
  files(
    'rgb_hand_math.hpp',
    'rgb_image_math.hpp',
    'rgb_interface.h',
    'rgb_model.hpp',
    'rgb_nms.hpp',
    'rgb_sync.cpp',
    'rgb_sync.hpp',
  ),
  include_directories: [xrt_include, cjson_include],
  dependencies: [aux, onnxruntime, opencv, eigen3]
)
