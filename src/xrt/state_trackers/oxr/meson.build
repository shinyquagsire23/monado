# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

compile_args = []
if x11.found()
	compile_args += ['-DXR_USE_GRAPHICS_API_OPENGL', '-DXR_USE_GRAPHICS_API_OPENGL_ES']
endif

if egl.found()
	compile_args += ['-DXR_USE_PLATFORM_EGL']
endif

if opengl.found()
	compile_args += ['-DXR_USE_PLATFORM_XLIB']
endif

lib_st_oxr = static_library(
	'st_oxr',
	files(
		'oxr_api_action.c',
		'oxr_api_funcs.h',
		'oxr_api_instance.c',
		'oxr_api_negotiate.c',
		'oxr_api_session.c',
		'oxr_api_space.c',
		'oxr_api_swapchain.c',
		'oxr_api_system.c',
		'oxr_api_verify.h',
		'oxr_binding.c',
		'oxr_chain.h',
		'oxr_event.c',
		'oxr_extension_support.h',
		'oxr_handle_base.c',
		'oxr_input.c',
		'oxr_instance.c',
		'oxr_logger.c',
		'oxr_logger.h',
		'oxr_objects.h',
		'oxr_path.c',
		'oxr_session.c',
		'oxr_session_gl.c',
		'oxr_session_egl.c',
		'oxr_session_vk.c',
		'oxr_space.c',
		'oxr_swapchain.c',
		'oxr_swapchain_gl.c',
		'oxr_swapchain_vk.c',
		'oxr_system.c',
		'oxr_two_call.h',
		'oxr_verify.c',
		'oxr_vulkan.c',
		'oxr_xdev.c',
	),
	include_directories: [
		xrt_include,
		external_include,
	],
	dependencies: [aux, vulkan],
	c_args: compile_args,
	cpp_args: compile_args,
)
