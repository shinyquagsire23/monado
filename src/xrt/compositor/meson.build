# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

subdir('shaders')

# TODO: Dependency resolution and subsequent configuration could be improved
compositor_deps = [aux, shaders, vulkan]

compositor_srcs = [
	'client/comp_gl_client.c',
	'client/comp_gl_client.h',
	'client/comp_vk_client.c',
	'client/comp_vk_client.h',
	'client/comp_xlib_client.c',
	'common/comp_vk.c',
	'common/comp_vk.h',
	'common/comp_vk_swapchain.h',
	'common/comp_vk_swapchain.c',
	'main/comp_client_interface.h',
	'main/comp_compositor.c',
	'main/comp_compositor.h',
	'main/comp_distortion.c',
	'main/comp_distortion.h',
	'main/comp_documentation.h',
	'main/comp_glue_egl.c',
	'main/comp_glue_gl.c',
	'main/comp_glue_vk.c',
	'main/comp_glue_xlib.c',
	'main/comp_renderer.c',
	'main/comp_renderer.h',
	'main/comp_settings.c',
	'main/comp_settings.h',
	'main/comp_swapchain.c',
	'main/comp_window.h',
]

compile_args = []

if xcb.found()
	compile_args += ['-DVK_USE_PLATFORM_XCB_KHR']
	compositor_srcs += ['main/comp_window_xcb.cpp']
	compositor_deps += [xcb]
endif

if xcb_randr.found()
	# TODO: monado doesn't compile when xcb is present but not xrandr
	compile_args += ['-DVK_USE_PLATFORM_XLIB_XRANDR_EXT']
	compositor_srcs += ['main/comp_window_direct_mode.cpp']
	compositor_deps += [xcb_randr]
endif

if wayland.found() and wayland_protos.found() and wayland_scanner.found()
	wl_protos_src = []
	wl_protos_headers = []
	wl_protocol_dir = wayland_protos.get_pkgconfig_variable('pkgdatadir')

	protocols = [
		[wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
	]

	foreach p : protocols
		xml = join_paths(p)
		wl_protos_src += custom_target(
			xml.underscorify() + '_c',
			input: xml,
			output: '@BASENAME@-protocol.c',
			command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
		)
		wl_protos_headers += custom_target(
			xml.underscorify() + '_client_h',
			input: xml,
			output: '@BASENAME@-client-protocol.h',
			command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
		)
	endforeach

	lib_wl_protos = static_library(
		'wl_protos',
		wl_protos_src + wl_protos_headers,
		dependencies: wayland.partial_dependency(compile_args: true),
	)

	wl_protos = declare_dependency(
		link_with: lib_wl_protos,
		sources: wl_protos_headers,
	)

	compile_args += ['-DVK_USE_PLATFORM_WAYLAND_KHR']
	compositor_srcs += ['main/comp_window_wayland.c']
	compositor_deps += [wayland, wl_protos]
endif

if get_option('vulkan-validation')
	compile_args += ['-DXRT_ENABLE_VK_VALIDATION']
endif

lib_comp = static_library(
	'comp',
	compositor_srcs,
	include_directories: [
		xrt_include,
		external_include,
	],
	dependencies: compositor_deps,
	c_args: compile_args,
	cpp_args: compile_args,
)
