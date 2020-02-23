# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

spirv_shaders(SHADER_HEADERS
	shaders/distortion.vert
	shaders/mesh.frag
	shaders/mesh.vert
	shaders/none.frag
	shaders/panotools.frag
	shaders/vive.frag
	)

set(SOURCE_FILES
	client/comp_vk_client.c
	client/comp_vk_client.h
	common/comp_vk_swapchain.h
	common/comp_vk_swapchain.c
	main/comp_client_interface.h
	main/comp_compositor.c
	main/comp_compositor.h
	main/comp_distortion.c
	main/comp_distortion.h
	main/comp_documentation.h
	main/comp_glue_vk.c
	main/comp_renderer.c
	main/comp_renderer.h
	main/comp_settings.c
	main/comp_settings.h
	main/comp_swapchain.c
	main/comp_window.h
	)

if(BUILD_WITH_XCB)
	list(APPEND SOURCE_FILES main/comp_window_xcb.cpp)
endif()
if(BUILD_WITH_XCB AND BUILD_WITH_XLIB)
	list(APPEND SOURCE_FILES main/comp_window_direct_mode.cpp)
endif()
if(BUILD_WITH_OPENGL)
	list(APPEND SOURCE_FILES
		client/comp_gl_client.c
		client/comp_gl_client.h
		main/comp_glue_gl.c
		main/comp_glue_gles.c # TODO separate config for GLES
		)
endif()
if(BUILD_WITH_OPENGL AND BUILD_WITH_XLIB)
	list(APPEND SOURCE_FILES
		client/comp_gl_xlib_client.c
		main/comp_glue_gl_xlib.c
		)

endif()
if(BUILD_WITH_OPENGL AND BUILD_WITH_EGL)
	list(APPEND SOURCE_FILES main/comp_glue_egl.c)
endif()

# generate wayland protocols
if(BUILD_WITH_WAYLAND)
	pkg_get_variable(WL_PROTOS_PKG_DIR wayland-protocols pkgdatadir)
	pkg_get_variable(WL_SCANNER wayland-scanner wayland_scanner)

	set(WL_PROTOS_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols/")
	file(MAKE_DIRECTORY "${WL_PROTOS_DIR}")

	set(WL_PROTOS_XML "${WL_PROTOS_PKG_DIR}/stable/xdg-shell/xdg-shell.xml")
	set(WL_PROTOS_C "${WL_PROTOS_DIR}/xdg-shell.c")
	set(WL_PROTOS_H "${WL_PROTOS_DIR}/xdg-shell-client-protocol.h")

	add_custom_command(
		COMMAND
		${WL_SCANNER} private-code "${WL_PROTOS_XML}" "${WL_PROTOS_C}"
		OUTPUT "${WL_PROTOS_C}" VERBATIM)

	add_custom_command(
		COMMAND
		${WL_SCANNER} client-header "${WL_PROTOS_XML}" "${WL_PROTOS_H}"
		OUTPUT "${WL_PROTOS_H}" VERBATIM)


	set(WL_PROTOS_SRC ${WL_PROTOS_C} ${WL_PROTOS_H})
	list(APPEND SOURCE_FILES main/comp_window_wayland.c)
endif()

if (${VULKAN_ENABLE_VALIDATION})
	add_definitions(-DXRT_ENABLE_VK_VALIDATION)
endif()

# Use OBJECT to not create a archive, since it just gets in the way.
add_library(comp OBJECT ${SHADER_HEADERS} ${SOURCE_FILES} ${WL_PROTOS_SRC})
target_include_directories(comp SYSTEM
	PRIVATE
	${VULKAN_INCLUDE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../../external
	# Shaders - marked SYSTEM so we get no warnings
	${CMAKE_CURRENT_BINARY_DIR}
)
target_include_directories(comp
	PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../include
	${CMAKE_CURRENT_SOURCE_DIR}/../auxiliary
	)

if(BUILD_WITH_WAYLAND)
	target_include_directories(comp SYSTEM PRIVATE ${WL_PROTOS_DIR})
endif()
if(BUILD_WITH_XCB)
	target_include_directories(comp SYSTEM PRIVATE ${XCB_INCLUDE_DIRS})
endif()
if(BUILD_WITH_EGL)
	target_include_directories(comp SYSTEM PRIVATE ${EGL_INCLUDE_DIRS})
endif()

add_subdirectory(shaders)
