# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

###
# Client library
#

add_library(comp_client STATIC)

target_link_libraries(
	comp_client
	PUBLIC xrt-interfaces
	PRIVATE aux_util
	)
target_include_directories(comp_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(XRT_HAVE_VULKAN)
	target_sources(
		comp_client PRIVATE client/comp_vk_client.c client/comp_vk_client.h
				    client/comp_vk_glue.c
		)
	target_link_libraries(comp_client PRIVATE aux_vk)
endif()

if(XRT_HAVE_OPENGL OR XRT_HAVE_OPENGLES)
	target_sources(
		comp_client
		PRIVATE client/comp_gl_client.c client/comp_gl_client.h
			client/comp_gl_memobj_swapchain.c client/comp_gl_memobj_swapchain.h
		)
	target_link_libraries(comp_client PRIVATE aux_ogl)
endif()

if(XRT_HAVE_OPENGL)
	target_sources(comp_client PRIVATE client/comp_gl_glue.c)
endif()

if(XRT_HAVE_OPENGLES)
	target_sources(comp_client PRIVATE client/comp_gles_glue.c)
endif()

if(XRT_HAVE_EGL)
	target_sources(
		comp_client PRIVATE client/comp_gl_eglimage_swapchain.c
				    client/comp_gl_eglimage_swapchain.h
		)
endif()

if(XRT_HAVE_OPENGL_GLX AND XRT_HAVE_XLIB)
	target_sources(
		comp_client PRIVATE client/comp_gl_xlib_client.c client/comp_gl_xlib_client.h
				    client/comp_gl_xlib_glue.c
		)
	target_link_libraries(comp_client PRIVATE OpenGL::GLX)
endif()

if(XRT_HAVE_OPENGL AND WIN32)
	target_sources(
		comp_client PRIVATE client/comp_gl_win32_client.c client/comp_gl_win32_client.h
				    client/comp_gl_win32_glue.c
		)
endif()
if(XRT_HAVE_EGL)
	target_sources(comp_client PRIVATE client/comp_egl_client.c client/comp_egl_client.h)
endif()

if(XRT_HAVE_D3D11 OR XRT_HAVE_D3D12)
	target_link_libraries(comp_client PRIVATE aux_d3d)
endif()

if(XRT_HAVE_D3D11 OR XRT_HAVE_D3D12)
	target_sources(comp_client PRIVATE client/comp_d3d_common.cpp client/comp_d3d_common.hpp)
endif()

if(XRT_HAVE_D3D11)
	target_sources(
		comp_client PRIVATE client/comp_d3d11_client.cpp client/comp_d3d11_client.h
				    client/comp_d3d11_glue.c
		)
endif()

if(XRT_HAVE_D3D12)
	target_sources(
		comp_client PRIVATE client/comp_d3d12_client.cpp client/comp_d3d12_client.h
				    client/comp_d3d12_glue.c
		)
endif()

##
# Util library
#

if(XRT_HAVE_VULKAN)
	add_library(
		comp_util STATIC
		util/comp_base.h
		util/comp_base.c
		util/comp_semaphore.h
		util/comp_semaphore.c
		util/comp_swapchain.h
		util/comp_swapchain.c
		util/comp_sync.h
		util/comp_sync.c
		util/comp_vulkan.h
		util/comp_vulkan.c
		)
	target_link_libraries(
		comp_util
		PUBLIC xrt-interfaces
		PRIVATE aux_util aux_os aux_vk
		)
	target_include_directories(comp_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()

##
# Render library
#

if(XRT_HAVE_VULKAN)
	set(SHADERS
	    shaders/clear.comp
	    shaders/distortion.comp
	    shaders/mesh.frag
	    shaders/mesh.vert
	    shaders/layer.frag
	    shaders/layer.vert
	    shaders/equirect1.vert
	    shaders/equirect1.frag
	    shaders/equirect2.vert
	    shaders/equirect2.frag
		)
	if(XRT_FEATURE_OPENXR_LAYER_CUBE)
		list(APPEND SHADERS shaders/cube.vert shaders/cube.frag)
	endif()

	spirv_shaders(
		SHADER_HEADERS
		SPIRV_VERSION
		1.0 # Currently targeting Vulkan 1.0
		SOURCES
		${SHADERS}
		)

	add_library(
		comp_render STATIC
		${SHADER_HEADERS}
		render/render_buffer.c
		render/render_compute.c
		render/render_gfx.c
		render/render_interface.h
		render/render_resources.c
		render/render_shaders.c
		render/render_util.c
		)
	target_link_libraries(
		comp_render
		PUBLIC xrt-interfaces
		PRIVATE aux_util aux_os aux_vk
		)
	target_include_directories(comp_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	# Shaders - marked SYSTEM so we get no warnings
	target_include_directories(comp_render SYSTEM PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

	add_subdirectory(shaders)
endif()

##
# Main library
#

if(XRT_FEATURE_COMPOSITOR_MAIN)

	add_library(
		comp_main STATIC
		main/comp_compositor.c
		main/comp_compositor.h
		main/comp_documentation.h
		main/comp_renderer.c
		main/comp_renderer.h
		main/comp_settings.c
		main/comp_settings.h
		main/comp_target.h
		main/comp_target_swapchain.c
		main/comp_target_swapchain.h
		main/comp_window.h
		main/comp_layer.h
		main/comp_layer.c
		main/comp_layer_renderer.h
		main/comp_layer_renderer.c
		)
	target_link_libraries(
		comp_main
		PUBLIC xrt-interfaces
		PRIVATE
			aux_util
			aux_os
			aux_vk
			comp_util
			comp_render
		)
	target_include_directories(comp_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

	if(XRT_HAVE_XCB)
		target_sources(comp_main PRIVATE main/comp_window_xcb.c)
		target_include_directories(comp_main SYSTEM PRIVATE ${XCB_INCLUDE_DIRS})
		target_link_libraries(comp_main PRIVATE ${XCB_LIBRARIES})
		if(XRT_HAVE_EGL)
			target_include_directories(comp_main SYSTEM PRIVATE ${EGL_INCLUDE_DIRS})
			target_link_libraries(comp_main PRIVATE ${XCB_LIBRARIES})
		endif()
	endif()
	if(XRT_HAVE_XCB AND XRT_HAVE_XLIB)
		target_sources(
			comp_main PRIVATE main/comp_window_direct_randr.c
					  main/comp_window_direct_nvidia.c
			)
		target_link_libraries(comp_main PRIVATE ${X11_X11_LIB})
	endif()
	if(XRT_FEATURE_WINDOW_PEEK)
		target_sources(comp_main PRIVATE main/comp_window_peek.c)
		target_link_libraries(comp_main PRIVATE ${SDL2_LIBRARIES})
	endif()
	if(WIN32)
		target_sources(comp_main PRIVATE main/comp_window_mswin.c)
	endif()
	if(VK_USE_PLATFORM_DISPLAY_KHR)
		target_sources(comp_main PRIVATE main/comp_window_vk_display.c)
	endif()
	if(VK_USE_PLATFORM_DISPLAY_KHR OR XRT_HAVE_XCB)
		target_sources(comp_main PRIVATE main/comp_window_direct.c)
	endif()

	# generate wayland protocols
	if(XRT_HAVE_WAYLAND)
		target_sources(comp_main PRIVATE main/comp_window_wayland.c)
		pkg_get_variable(WL_PROTOS_PKG_DIR wayland-protocols pkgdatadir)
		pkg_get_variable(WL_SCANNER wayland-scanner wayland_scanner)

		set(WL_PROTOS_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
		file(MAKE_DIRECTORY "${WL_PROTOS_DIR}")

		set(WL_PROTOS_XML ${WL_PROTOS_PKG_DIR}/stable/xdg-shell/xdg-shell.xml)

		target_include_directories(comp_main SYSTEM PRIVATE ${WL_PROTOS_DIR})
		target_link_libraries(comp_main PRIVATE ${WAYLAND_LIBRARIES})

		if(XRT_HAVE_WAYLAND_DIRECT)
			list(
				APPEND WL_PROTOS_XML
				${WL_PROTOS_PKG_DIR}/staging/drm-lease/drm-lease-v1.xml
				)

			target_sources(comp_main PRIVATE main/comp_window_direct_wayland.c)

			target_link_libraries(comp_main PRIVATE PkgConfig::LIBDRM)
		endif()

		foreach(wl_proto_xml ${WL_PROTOS_XML})
			get_filename_component(WL_PROTO ${wl_proto_xml} NAME_WE)

			set(WL_PROTO_C "${WL_PROTOS_DIR}/${WL_PROTO}.c")
			set(WL_PROTO_H "${WL_PROTOS_DIR}/${WL_PROTO}-client-protocol.h")

			add_custom_command(
				OUTPUT "${WL_PROTO_C}"
				COMMAND
					"${WL_SCANNER}" private-code "${wl_proto_xml}"
					"${WL_PROTO_C}"
				VERBATIM
				DEPENDS "${WL_SCANNER}" "${wl_proto_xml}"
				COMMENT "Generating ${WL_PROTO_C}"
				)

			add_custom_command(
				OUTPUT "${WL_PROTO_H}"
				COMMAND
					"${WL_SCANNER}" client-header "${wl_proto_xml}"
					"${WL_PROTO_H}"
				VERBATIM
				DEPENDS "${WL_SCANNER}" "${wl_proto_xml}"
				COMMENT "Generating ${WL_PROTO_H}"
				)

			target_sources(comp_main PRIVATE ${WL_PROTO_C} ${WL_PROTO_H})
		endforeach()

	endif()
	if(ANDROID)
		target_sources(comp_main PRIVATE main/comp_window_android.c)
		target_link_libraries(comp_main PRIVATE aux_ogl aux_android)
	endif()
endif()

###
# Null compositor
#

if(XRT_FEATURE_COMPOSITOR_NULL)
	add_subdirectory(null)
endif()

###
# Multi client compositor library
#

add_library(
	comp_multi STATIC multi/comp_multi_compositor.c multi/comp_multi_interface.h
			  multi/comp_multi_private.h multi/comp_multi_system.c
	)
target_link_libraries(
	comp_multi
	PUBLIC xrt-interfaces
	PRIVATE aux_util aux_os
	)
target_include_directories(comp_multi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(XRT_FEATURE_COMPOSITOR_MAIN)
	target_link_libraries(comp_main PRIVATE comp_multi)
endif()

###
# Mock native compositor
add_library(comp_mock STATIC mock/mock_compositor.cpp mock/mock_compositor.h)
target_include_directories(comp_mock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
	comp_mock
	PUBLIC xrt-interfaces
	PRIVATE aux_util
	)
