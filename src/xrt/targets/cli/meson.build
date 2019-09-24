# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

cli = executable(
	'monado-cli',
	files(
		'cli_cmd_calibrate.c',
		'cli_cmd_test.c',
		'cli_common.h',
		'cli_main.c',
		'cli_prober.c',
	),
	link_whole: [
		lib_aux_os,
		lib_aux_util,
		lib_aux_math,
		lib_st_prober,
		lib_target_lists,
	] + driver_libs,
	include_directories: [
		aux_include,
		common_include,
		drv_include,
		xrt_include,
	],
	dependencies: [
		libusb,
		libuvc,
		pthreads,
		udev,
	] + driver_deps,
)
