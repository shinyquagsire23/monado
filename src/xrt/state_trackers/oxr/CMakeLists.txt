# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


###
# Binding generation
#

function(bindings_gen output)
	add_custom_command(OUTPUT ${output}
		COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bindings.py
			${CMAKE_CURRENT_SOURCE_DIR}/bindings.json
			${output}
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bindings.py
			${CMAKE_CURRENT_SOURCE_DIR}/bindings.json
			)
endfunction(bindings_gen)

bindings_gen(${CMAKE_CURRENT_BINARY_DIR}/oxr_generated_bindings.h)
bindings_gen(${CMAKE_CURRENT_BINARY_DIR}/oxr_generated_bindings.c)


###
# Main code
#

set(OXR_SOURCE_FILES
	${CMAKE_CURRENT_BINARY_DIR}/oxr_generated_bindings.h
	${CMAKE_CURRENT_BINARY_DIR}/oxr_generated_bindings.c
	oxr_api_action.c
	oxr_api_funcs.h
	oxr_api_instance.c
	oxr_api_negotiate.c
	oxr_api_session.c
	oxr_api_space.c
	oxr_api_swapchain.c
	oxr_api_system.c
	oxr_api_verify.h
	oxr_binding.c
	oxr_chain.h
	oxr_event.c
	oxr_extension_support.h
	oxr_handle_base.c
	oxr_input.c
	oxr_input_transform.c
	oxr_input_transform.h
	oxr_instance.c
	oxr_logger.c
	oxr_logger.h
	oxr_objects.h
	oxr_path.c
	oxr_session.c
	oxr_space.c
	oxr_swapchain.c
	oxr_system.c
	oxr_two_call.h
	oxr_verify.c
	oxr_xdev.c
	)

if(XRT_HAVE_VULKAN)
	list(APPEND OXR_SOURCE_FILES
		oxr_session_vk.c
		oxr_swapchain_vk.c
		oxr_vulkan.c
		)
endif()

if(XRT_HAVE_OPENGL)
	add_definitions(-DXR_USE_GRAPHICS_API_OPENGL)
endif()
if(XRT_HAVE_OPENGLES)
	add_definitions(-DXR_USE_GRAPHICS_API_OPENGL_ES)
endif()

if(XRT_HAVE_OPENGL OR XRT_HAVE_OPENGLES)
	list(APPEND OXR_SOURCE_FILES
		oxr_session_gl.c
		oxr_swapchain_gl.c
		)
endif()

if(XRT_HAVE_XLIB)
	add_definitions(-DXR_USE_PLATFORM_XLIB)
endif()

if(XRT_HAVE_EGL)
	add_definitions(-DXR_USE_PLATFORM_EGL)
	list(APPEND OXR_SOURCE_FILES oxr_session_egl.c)
endif()


if(ANDROID)
	add_definitions(-DXR_USE_PLATFORM_ANDROID)
endif()

add_library(st_oxr STATIC ${OXR_SOURCE_FILES})
target_link_libraries(st_oxr PRIVATE
	xrt-interfaces
	xrt-external-openxr
	aux_util
	aux_math
	comp_client
	)
if(XRT_HAVE_VULKAN)
	target_link_libraries(st_oxr PRIVATE Vulkan::Vulkan)
endif()
target_include_directories(st_oxr
	PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}
	INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/..)
