# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

st_ovrd_include = include_directories('.')

lib_st_ovrd = static_library(
	'st_ovrd',
	files(
		'ovrd_driver.cpp',
		'ovrd_interface.h'
	),
	include_directories: [
		openvr_include,
		st_include, # Sigh debian meson requires this.
		xrt_include,
	],
	dependencies: aux_util,
	c_args: compile_args,
	cpp_args: compile_args,
)
