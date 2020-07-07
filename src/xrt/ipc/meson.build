# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


###
# Generated

prog_python = import('python').find_installation('python3')

common_sources = [
	'ipc_utils.c',
	'ipc_utils.h',
]

generated = custom_target('protocol code',
	command: [prog_python, '@INPUT@', '@OUTPUT@'],
	input: ['proto.py', 'proto.json'],
	output: [
		'ipc_protocol_generated.h',
		'ipc_client_generated.c',
		'ipc_client_generated.h',
		'ipc_server_generated.c',
		'ipc_server_generated.h',
	]
)


###
# Client

lib_ipc_client = static_library(
	'ipc_client',
	common_sources + [
		generated[0],
		generated[1],
		generated[2],
		'ipc_client.h',
		'ipc_client_compositor.c',
		'ipc_client_device.c',
		'ipc_client_hmd.c',
		'ipc_client_instance.c',
	],
	include_directories: [
		xrt_include,
	],
	dependencies: [aux]
)


###
# Server

lib_ipc_server = static_library(
	'ipc_server',
	common_sources + [
		generated[0],
		generated[3],
		generated[4],
		'ipc_server.h',
		'ipc_server_client.c',
		'ipc_server_process.c',
	],
	include_directories: [
		xrt_include,
		comp_include,
	],
	dependencies: [aux_util, rt, aux_vk, aux_ogl]
)
