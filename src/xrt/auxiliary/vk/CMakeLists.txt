# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(
	aux_vk STATIC
	vk_bundle_init.c
	vk_compositor_flags.c
	vk_documentation.h
	vk_function_loaders.c
	vk_helpers.c
	vk_helpers.h
	vk_image_allocator.c
	vk_image_allocator.h
	vk_image_readback_to_xf_pool.c
	vk_image_readback_to_xf_pool.h
	vk_print.c
	vk_state_creators.c
	vk_sync_objects.c
	vk_time.c
	)
target_link_libraries(aux_vk PUBLIC aux_os aux_util)
target_link_libraries(aux_vk PUBLIC Vulkan::Vulkan)
target_include_directories(aux_vk PUBLIC ${Vulkan_INCLUDE_DIR})

if(ANDROID)
	target_link_libraries(aux_vk PUBLIC aux_android)
	target_link_libraries(aux_vk PUBLIC ${ANDROID_LIBRARY})
endif()

if(XRT_HAVE_WAYLAND)
	target_include_directories(aux_vk PRIVATE ${WAYLAND_INCLUDE_DIR})
endif()
if(XRT_HAVE_XCB)
	target_include_directories(aux_vk PRIVATE ${XCB_INCLUDE_DIR})
endif()
if(XRT_HAVE_XRANDR)
	target_include_directories(aux_vk PRIVATE ${XRANDR_INCLUDE_DIR} ${X11_INCLUDE_DIR})
endif()
