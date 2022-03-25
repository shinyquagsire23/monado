# Copyright 2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


# Mercury hand tracking library!

t_ht_mercury_kine = static_library(
	't_ht_mercury_kine',
	files(
		'kine/kinematic_interface.hpp',
		'kine/kinematic_main.cpp'
	),
	include_directories: [xrt_include, cjson_include],
	dependencies: [aux, eigen3]
)

t_ht_mercury = static_library(
	't_ht_mercury',
	files(
		'hg_interface.h',
		'hg_model.hpp',
		'hg_sync.cpp',
		'hg_sync.hpp',
	),
	link_with: [t_ht_mercury_kine],
	include_directories: [xrt_include, cjson_include],
	dependencies: [aux, onnxruntime, opencv, eigen3]
)
