# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

service = executable(
	'monado-service',
	['main.c'],
	link_whole: [
		lib_st_prober,
		lib_ipc_server,
		lib_comp,
		lib_target_lists,
		lib_target_instance,
		lib_aux_util,
	] + driver_libs,
	include_directories: [
		aux_include,
		common_include,
		xrt_include,
	],
	dependencies: [pthreads, libjpeg],
	install: true,
)
