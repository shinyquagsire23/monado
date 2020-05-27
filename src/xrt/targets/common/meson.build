# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

common_include = include_directories('.')

lib_target_lists = static_library(
	'target_lists',
	files(
		'target_lists.c',
	),
	include_directories: [drv_include, xrt_include],
	dependencies: [aux, xrt_config_drivers],
)

lib_target_instance = static_library(
	'target_instance',
	files(
		'target_instance.c',
	),
	include_directories: [xrt_include],
	link_with: [lib_target_lists, lib_st_prober],
	dependencies: [aux],
)

lib_target_instance_no_comp = static_library(
	'target_instance_no_comp',
	files(
		'target_instance_no_comp.c',
	),
	include_directories: [xrt_include],
	link_with: [lib_target_lists, lib_st_prober],
	dependencies: [aux],
)
