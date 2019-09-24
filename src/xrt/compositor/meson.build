# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

subdir('shaders')

compositor_deps = [aux, shaders, vulkan]

compositor_srcs = [
	'client/comp_gl_api.c',
	'client/comp_gl_api.h',
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

# TODO: This backend is straight-up broken
#if wayland.found()
#	compile_args += ['-DVK_USE_PLATFORM_WAYLAND_KHR']
#	compositor_srcs += ['main/comp_window_wayland.cpp']
#	compositor_deps += [wayland]
#endif

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
