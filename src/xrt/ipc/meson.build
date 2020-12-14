# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

ipc_include = include_directories('.')

###
# Generated

prog_python = import('python').find_installation('python3')

common_sources = [
	'shared/ipc_shmem.c',
	'shared/ipc_shmem.h',
	'shared/ipc_utils.c',
	'shared/ipc_utils.h',
]

generated = custom_target('protocol code',
	command: [prog_python, '@INPUT@', '@OUTPUT@'],
	input: ['shared/proto.py', 'shared/proto.json'],
	output: [
		'ipc_protocol_generated.h',
		'ipc_client_generated.c',
		'ipc_client_generated.h',
		'ipc_server_generated.c',
		'ipc_server_generated.h',
	]
)

ipc_generated_client_header_target = generated[2]

###
# Client

lib_ipc_client = static_library(
	'ipc_client',
	common_sources + [
		generated[0],
		generated[1],
		generated[2],
		'client/ipc_client.h',
		'client/ipc_client_compositor.c',
		'client/ipc_client_device.c',
		'client/ipc_client_hmd.c',
		'client/ipc_client_instance.c',
	],
	include_directories: [
		xrt_include,
	],
	dependencies: [aux, rt]
)


###
# Server

lib_ipc_server = static_library(
	'ipc_server',
	common_sources + [
		generated[0],
		generated[3],
		generated[4],
		'server/ipc_server.h',
		'server/ipc_server_handler.c',
		'server/ipc_server_per_client_thread.c',
		'server/ipc_server_process.c',
	],
	include_directories: [
		xrt_include,
		comp_include,
		glad_include,
	],
	dependencies: [aux_util, rt, aux_vk, aux_ogl]
)
