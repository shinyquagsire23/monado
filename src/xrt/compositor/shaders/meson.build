# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

shader_srcs = [
	'mesh.frag',
	'mesh.vert',
	'layer.vert',
	'layer.frag',
	'equirect1.vert',
	'equirect1.frag',
	'equirect2.vert',
	'equirect2.frag'
]

shader_headers = []
foreach shader : shader_srcs
	header_out = shader + '.h'
	shader_headers += custom_target(
		header_out,
		command: [
			glslangValidator,
			'-V', '@INPUT@',
			'-o', '@OUTPUT@',
			'--vn', 'shaders_' + shader.underscorify(),
		],
		input: shader,
		output: header_out,
	)
endforeach

shaders = declare_dependency(
	include_directories: include_directories('..'),
	sources: shader_headers,
)
