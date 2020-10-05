# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

spirv_shaders(SHADER_HEADERS
	shaders/mesh.frag
	shaders/mesh.vert
	shaders/layer.frag
	shaders/layer.vert
	)

set(CLIENT_SOURCE_FILES)

set(MAIN_SOURCE_FILES
	main/comp_compositor.c
	main/comp_compositor.h
	main/comp_documentation.h
	main/comp_renderer.c
	main/comp_renderer.h
	main/comp_settings.c
	main/comp_settings.h
	main/comp_shaders.c
	main/comp_swapchain.c
	main/comp_vk_swapchain.c
	main/comp_vk_swapchain.h
	main/comp_window.h
	main/comp_layer.h
	main/comp_layer.c
	main/comp_layer_renderer.h
	main/comp_layer_renderer.c
	render/comp_buffer.c
	render/comp_render.h
	render/comp_rendering.c
	render/comp_resources.c
	)

###
# Client library
#

if(XRT_HAVE_VULKAN)
	list(APPEND CLIENT_SOURCE_FILES
		client/comp_vk_client.c
		client/comp_vk_client.h
		client/comp_vk_glue.c
		)
endif()
if(XRT_HAVE_OPENGL OR XRT_HAVE_OPENGLES)
	list(APPEND CLIENT_SOURCE_FILES
		client/comp_gl_client.c
		client/comp_gl_client.h
		client/comp_gl_memobj_swapchain.c
		client/comp_gl_memobj_swapchain.h
		)
endif()
if(XRT_HAVE_OPENGL)
	list(APPEND CLIENT_SOURCE_FILES
		client/comp_gl_glue.c
		)
endif()
if(XRT_HAVE_OPENGLES)
	list(APPEND CLIENT_SOURCE_FILES
		client/comp_gles_glue.c
		client/comp_gl_eglimage_swapchain.c
		client/comp_gl_eglimage_swapchain.h
		)
endif()
if(XRT_HAVE_OPENGL AND XRT_HAVE_XLIB)
	list(APPEND CLIENT_SOURCE_FILES
		client/comp_gl_xlib_client.c
		client/comp_gl_xlib_client.h
		client/comp_gl_xlib_glue.c
		)
endif()
if(XRT_HAVE_EGL)
	list(APPEND CLIENT_SOURCE_FILES
		client/comp_egl_glue.c
		)
endif()

add_library(comp_client STATIC ${CLIENT_SOURCE_FILES})
target_link_libraries(comp_client PUBLIC xrt-interfaces PRIVATE aux_util)
target_include_directories(comp_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(XRT_HAVE_VULKAN)
	target_link_libraries(comp_client PRIVATE aux_vk)
endif()
if(XRT_HAVE_OPENGL OR XRT_HAVE_OPENGLES)
	target_link_libraries(comp_client PRIVATE aux_ogl)
endif()

if(XRT_HAVE_OPENGL AND XRT_HAVE_XLIB)
	target_link_libraries(comp_client PRIVATE OpenGL::GLX)
endif()
##
# Main library
#

if(XRT_FEATURE_COMPOSITOR_MAIN)

	if(XRT_HAVE_XCB)
		list(APPEND MAIN_SOURCE_FILES
			main/comp_window_xcb.c
			)
	endif()
	if(XRT_HAVE_XCB AND XRT_HAVE_XLIB)
		list(APPEND MAIN_SOURCE_FILES
			main/comp_window_direct.c
			main/comp_window_direct_randr.c
			main/comp_window_direct_nvidia.c
			)
	endif()

	# generate wayland protocols
	if(XRT_HAVE_WAYLAND)
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
		list(APPEND MAIN_SOURCE_FILES
			main/comp_window_wayland.c
			)
	endif()

	add_library(comp_main STATIC ${SHADER_HEADERS} ${MAIN_SOURCE_FILES} ${WL_PROTOS_SRC})
	target_link_libraries(comp_main PUBLIC xrt-interfaces PRIVATE aux_util aux_os aux_vk)
	target_include_directories(comp_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_include_directories(comp_main SYSTEM PRIVATE
		# Shaders - marked SYSTEM so we get no warnings
		${CMAKE_CURRENT_BINARY_DIR}
		)

	if(XRT_HAVE_WAYLAND)
		target_include_directories(comp_main SYSTEM PRIVATE ${WL_PROTOS_DIR})
		target_link_libraries(comp_main PRIVATE ${WAYLAND_LIBRARIES})
	endif()
	if(XRT_HAVE_XCB)
		target_include_directories(comp_main SYSTEM PRIVATE ${XCB_INCLUDE_DIRS})
		target_link_libraries(comp_main PRIVATE ${XCB_LIBRARIES})
	endif()

	if(XRT_HAVE_XCB AND XRT_HAVE_XLIB)
		target_link_libraries(comp_main PRIVATE ${X11_X11_LIB})
	endif()
	if(XRT_HAVE_EGL)
		target_include_directories(comp_main SYSTEM PRIVATE ${EGL_INCLUDE_DIRS})
		target_link_libraries(comp_main PRIVATE ${XCB_LIBRARIES})
	endif()

	add_subdirectory(shaders)
endif()
