# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

ctl = executable(
	'monado-ctl',
	files(
		'main.c',
	) + [ ipc_generated_client_header_target ],
	link_whole: [
		lib_ipc_client,
	],
	include_directories: [
		xrt_include,
		aux_include,
		ipc_include,
	],
	dependencies: [
	],
	install: true,
)
