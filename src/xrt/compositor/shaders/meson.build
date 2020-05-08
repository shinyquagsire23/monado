shader_srcs = [
	'distortion.vert',
	'mesh.frag',
	'mesh.vert',
	'none.frag',
	'panotools.frag',
	'vive.frag',
	'quad.vert',
	'quad.frag'
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
