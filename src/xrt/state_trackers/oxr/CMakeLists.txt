# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../../include
	${CMAKE_CURRENT_SOURCE_DIR}/../../auxiliary
	${CMAKE_CURRENT_SOURCE_DIR}/../../../external
	)

set(OXR_SOURCE_FILES
	oxr_api_action.c
	oxr_api_debug.c
	oxr_api_funcs.h
	oxr_api_instance.c
	oxr_api_negotiate.c
	oxr_api_session.c
	oxr_api_space.c
	oxr_api_swapchain.c
	oxr_api_system.c
	oxr_api_verify.h
	oxr_event.cpp
	oxr_handle_base.c
	oxr_instance.c
	oxr_logger.cpp
	oxr_logger.h
	oxr_messenger.c
	oxr_objects.h
	oxr_path.c
	oxr_session.c
	oxr_session_gl.c
	oxr_session_vk.c
	oxr_space.c
	oxr_swapchain.c
	oxr_swapchain_gl.c
	oxr_swapchain_vk.c
	oxr_system.c
	oxr_two_call.h
	oxr_verify.cpp
	oxr_vulkan.c
	)

# Use OBJECT to not create a archive, since it just gets in the way.
add_library(st_oxr OBJECT ${OXR_SOURCE_FILES})

target_include_directories(st_oxr
	PRIVATE
	${VULKAN_INCLUDE_DIR}
	)

set_property(TARGET st_oxr PROPERTY POSITION_INDEPENDENT_CODE ON)
