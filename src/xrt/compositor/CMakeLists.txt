# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

spirv_shaders(SHADER_HEADERS
	shaders/distortion.vert
	shaders/none.frag
	shaders/panotools.frag
	shaders/vive.frag
	)

set(GL_SOURCE_FILES
	${SHADER_HEADERS}
	client/comp_gl_api.c
	client/comp_gl_api.h
	client/comp_gl_client.c
	client/comp_gl_client.h
	client/comp_vk_client.c
	client/comp_vk_client.h
	client/comp_xlib_client.c
	common/comp_vk.c
	common/comp_vk.h
	common/comp_vk_swapchain.h
	common/comp_vk_swapchain.c
	main/comp_client_interface.h
	main/comp_compositor.c
	main/comp_compositor.h
	main/comp_distortion.c
	main/comp_distortion.h
	main/comp_documentation.h
	main/comp_glue_gl.c
	main/comp_glue_vk.c
	main/comp_glue_xlib.c
	main/comp_renderer.c
	main/comp_renderer.h
	main/comp_settings.c
	main/comp_settings.h
	main/comp_swapchain.c
	main/comp_window.h
	main/comp_window_direct_mode.cpp
	main/comp_window_xcb.cpp
	)

if (${XCB_FOUND})
	add_definitions(-DVK_USE_PLATFORM_XCB_KHR)
	add_definitions(-DVK_USE_PLATFORM_XLIB_XRANDR_EXT)
endif()

if (${VULKAN_ENABLE_VALIDATION})
	add_definitions(-DXRT_ENABLE_VK_VALIDATION)
endif()

# Use OBJECT to not create a archive, since it just gets in the way.
add_library(comp OBJECT ${GL_SOURCE_FILES})
set_property(TARGET comp PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(comp
	PRIVATE
	${XCB_INCLUDE_DIRS}
	${VULKAN_INCLUDE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../include
	${CMAKE_CURRENT_SOURCE_DIR}/../auxiliary
	${CMAKE_CURRENT_SOURCE_DIR}/../../external
	${CMAKE_CURRENT_BINARY_DIR}
	)

add_subdirectory(shaders)
