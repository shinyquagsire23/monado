// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to vulkan client side glue code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp
 */

#include <stdlib.h>

#include "main/comp_client_interface.h"
#include "client/comp_vk_client.h"


const char *xrt_gfx_vk_instance_extensions =
    "VK_KHR_external_fence_capabilities "
    "VK_KHR_external_memory_capabilities "
    "VK_KHR_external_semaphore_capabilities "
    "VK_KHR_get_physical_device_properties2 "
    "VK_KHR_surface";

const char *xrt_gfx_vk_device_extensions =
    "VK_KHR_dedicated_allocation "
    "VK_KHR_external_fence "
    "VK_KHR_external_fence_fd "
    "VK_KHR_external_memory "
    "VK_KHR_external_memory_fd "
    "VK_KHR_external_semaphore "
    "VK_KHR_external_semaphore_fd "
    "VK_KHR_get_memory_requirements2 "
    "VK_KHR_swapchain";

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
xrt_gfx_vk_provider_create(struct xrt_device *xdev,
                           struct time_state *timekeeping,
                           VkInstance instance,
                           PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                           VkPhysicalDevice physical_device,
                           VkDevice device,
                           uint32_t queue_family_index,
                           uint32_t queue_index)
{
	struct xrt_compositor_fd *xcfd =
	    comp_compositor_create(xdev, timekeeping, false);
	if (xcfd == NULL) {
		return NULL;
	}

	struct client_vk_compositor *vcc = client_vk_compositor_create(
	    xcfd, instance, get_instance_proc_addr, physical_device, device,
	    queue_family_index, queue_index);

	return &vcc->base;
}
