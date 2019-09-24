# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

common_include = include_directories('.')

lib_target_lists = static_library(
	'target_lists',
	files(
		'target_lists.c',
	),
	include_directories: [drv_include, xrt_include],
	dependencies: [aux, targets_enabled],
)
