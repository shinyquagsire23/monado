// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to vulkan client side code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include "client/comp_vk_client.h"

#include <stdlib.h>

// If you update either list of extensions here, please update the "Client"
// column in `vulkan-extensions.md`

// Note: Most of the time, the instance extensions required do **not** vary by
// platform!
const char *xrt_gfx_vk_instance_extensions = VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME
    " " VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME " " VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME
    " " VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

// The device extensions do vary by platform, but in a very regular way.
// This should match the list in comp_compositor, except it shouldn't include
// VK_KHR_SWAPCHAIN_EXTENSION_NAME
const char *xrt_gfx_vk_device_extensions = VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME
    " " VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME " " VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME
    " " VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME " " VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME

// Platform version of "external_memory"
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
    " " VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
    " " VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)
    " " VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME
#else
#error "Need port!"
#endif

// Platform version of "external_fence" and "external_semaphore"
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
    " " VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME " " VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME;

#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
    " " VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME " " VK_KHR_EXTERNAL_FENCE_WIN32_EXTENSION_NAME;

#else
#error "Need port!"
#endif

void
xrt_gfx_vk_get_versions(struct xrt_api_requirements *ver)
{
	ver->min_major = 1;
	ver->min_minor = 0;
	ver->min_patch = 0;

	ver->max_major = (1024 - 1);
	ver->max_minor = (1024 - 1);
	ver->max_patch = (1024 - 1);
}

struct xrt_compositor_vk *
xrt_gfx_vk_provider_create(struct xrt_compositor_native *xcn,
                           VkInstance instance,
                           PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                           VkPhysicalDevice physical_device,
                           VkDevice device,
                           uint32_t queue_family_index,
                           uint32_t queue_index)
{
	struct client_vk_compositor *vcc = client_vk_compositor_create(
	    xcn, instance, get_instance_proc_addr, physical_device, device, queue_family_index, queue_index);

	return &vcc->base;
}
