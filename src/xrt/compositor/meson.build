# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

subdir('shaders')

comp_include = include_directories('.')

# TODO: Dependency resolution and subsequent configuration could be improved
compositor_deps = [aux, shaders, vulkan, xrt_config_vulkan, xcb_randr]
compositor_includes = [xrt_include]

compositor_srcs = [
	'client/comp_vk_client.c',
	'client/comp_vk_client.h',
	'client/comp_vk_glue.c',
	'main/comp_compositor.c',
	'main/comp_compositor.h',
	'main/comp_documentation.h',
	'main/comp_renderer.c',
	'main/comp_renderer.h',
	'main/comp_settings.c',
	'main/comp_settings.h',
	'main/comp_shaders.c',
	'main/comp_swapchain.c',
	'main/comp_target.h',
	'main/comp_target_swapchain.c',
	'main/comp_target_swapchain.h',
	'main/comp_window.h',
	'main/comp_layer_renderer.c',
	'main/comp_layer.c',
	'render/comp_buffer.c',
	'render/comp_render.h',
	'render/comp_rendering.c',
	'render/comp_resources.c',
]

compile_args = []

if build_vk_khr_display
	compositor_srcs += ['main/comp_window_vk_display.c']
endif

if build_xcb
	compositor_srcs += ['main/comp_window_xcb.c']
	compositor_deps += [xcb]
endif

if build_xcb_xrandr_direct
	compositor_srcs += ['main/comp_window_direct.c',
	                    'main/comp_window_direct_randr.c',
	                    'main/comp_window_direct_nvidia.c']
	compositor_deps += [xcb_randr, x11_xcb]
endif

if build_opengl or build_opengles
	compositor_srcs += [
		'client/comp_gl_client.c',
		'client/comp_gl_client.h',
		'client/comp_gl_memobj_swapchain.c',
		'client/comp_gl_memobj_swapchain.h',
	]
endif

if build_opengl
	compositor_srcs += [
		'client/comp_gl_glue.c',
	]
	compositor_deps += [opengl]
	compositor_includes += [glad_include]
endif
if build_opengles
	compositor_srcs += [
		'client/comp_gles_glue.c',
	]
	compositor_deps += [opengles]
	compositor_includes += [glad_include]
endif
if build_egl
	compositor_srcs += [
		'client/comp_gl_eglimage_swapchain.c',
		'client/comp_gl_eglimage_swapchain.h',
	]
endif

if build_opengl and build_xlib
	compositor_srcs += [
		'client/comp_gl_xlib_client.c',
		'client/comp_gl_xlib_client.h',
		'client/comp_gl_xlib_glue.c',
	]
	compositor_deps += [x11]
endif

if build_egl
	compositor_srcs += [
		'client/comp_egl_glue.c',
	]
	compositor_deps += [egl]
endif

if build_wayland
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

	compositor_srcs += ['main/comp_window_wayland.c']
	compositor_deps += [wayland, wl_protos]
endif

lib_comp = static_library(
	'comp',
	compositor_srcs,
	include_directories: compositor_includes,
	dependencies: compositor_deps,
	c_args: compile_args,
	cpp_args: compile_args,
)
