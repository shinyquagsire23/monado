# Copyright 2019-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

###
# Binding generation
#

aux_bindings_dir = meson.current_source_dir()

prog_python = import('python').find_installation('python3')

generated_bindings = custom_target('bindings code',
	command: [prog_python, '@INPUT@', '@OUTPUT@'],
	input: ['bindings.py', 'bindings.json'],
	output: [
		'b_generated_bindings.h',
		'b_generated_bindings.c',
	]
)

generated_bindings_h = generated_bindings[0]
generated_bindings_c = generated_bindings[1]

lib_aux_generated_bindings = static_library(
	'aux_generated_bindings',
	generated_bindings_c,
	include_directories: [
		xrt_include,
		aux_include,
	],
	dependencies: [
		xrt_config_have,
	]
)

aux_generated_bindings = declare_dependency(
	include_directories: aux_include,
	link_with: lib_aux_generated_bindings,
)
