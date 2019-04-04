# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../include
	${CMAKE_CURRENT_SOURCE_DIR}/../auxiliary
	${CMAKE_CURRENT_SOURCE_DIR}/../../external
	)

set(OXR_SOURCE_FILES
	oxr/oxr_api_action.c
	oxr/oxr_api_debug.c
	oxr/oxr_api_funcs.h
	oxr/oxr_api_instance.c
	oxr/oxr_api_negotiate.c
	oxr/oxr_api_session.c
	oxr/oxr_api_space.c
	oxr/oxr_api_swapchain.c
	oxr/oxr_api_system.c
	oxr/oxr_api_verify.h
	oxr/oxr_event.cpp
	oxr/oxr_handle_base.c
	oxr/oxr_instance.c
	oxr/oxr_logger.cpp
	oxr/oxr_logger.h
	oxr/oxr_messenger.c
	oxr/oxr_objects.h
	oxr/oxr_session.c
	oxr/oxr_session_gl.c
	oxr/oxr_session_vk.c
	oxr/oxr_space.c
	oxr/oxr_swapchain.c
	oxr/oxr_swapchain_gl.c
	oxr/oxr_swapchain_vk.c
	oxr/oxr_system.c
	oxr/oxr_two_call.h
	oxr/oxr_verify.cpp
	oxr/oxr_vulkan.c
	)

# Use OBJECT to not create a archive, since it just gets in the way.
add_library(st_oxr OBJECT ${OXR_SOURCE_FILES})

target_include_directories(st_oxr
	PRIVATE
	${VULKAN_INCLUDE_DIR}
	)

set_property(TARGET st_oxr PROPERTY POSITION_INDEPENDENT_CODE ON)
