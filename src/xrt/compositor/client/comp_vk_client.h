// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan client side glue to compositor header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_client
 */

#pragma once

#include "vk/vk_helpers.h"
#include "xrt/xrt_gfx_vk.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Structs
 *
 */

struct client_vk_compositor;

/*!
 * Wraps the real compositor swapchain providing a Vulkan based interface.
 *
 * Almost a one to one mapping to a OpenXR swapchain.
 *
 * @ingroup comp_client
 * @implements xrt_swapchain_vk
 */
struct client_vk_swapchain
{
	struct xrt_swapchain_vk base;

	//! Owning reference to the backing native swapchain.
	struct xrt_swapchain_native *xscn;

	//! Non-owning reference to our parent compositor.
	struct client_vk_compositor *c;

	// Memory
	VkDeviceMemory mems[XRT_MAX_SWAPCHAIN_IMAGES];

	// Prerecorded swapchain image ownership/layout transition barriers
	VkCommandBuffer acquire[XRT_MAX_SWAPCHAIN_IMAGES];
	VkCommandBuffer release[XRT_MAX_SWAPCHAIN_IMAGES];
};

/*!
 * @class client_vk_compositor
 *
 * Wraps the real compositor providing a Vulkan based interface.
 *
 * @ingroup comp_client
 * @implements xrt_compositor_vk
 */
struct client_vk_compositor
{
	struct xrt_compositor_vk base;

	//! Owning reference to the backing native compositor
	struct xrt_compositor_native *xcn;

	struct vk_bundle vk;
};


/*
 *
 * Functions and helpers.
 *
 */


/*!
 * Create a new client_vk_compositor.
 *
 * Takes owenership of provided xcn.
 *
 * @public @memberof client_vk_compositor
 * @relatesalso xrt_compositor_native
 */
struct client_vk_compositor *
client_vk_compositor_create(struct xrt_compositor_native *xcn,
                            VkInstance instance,
                            PFN_vkGetInstanceProcAddr getProc,
                            VkPhysicalDevice physicalDevice,
                            VkDevice device,
                            uint32_t queueFamilyIndex,
                            uint32_t queueIndex);


#ifdef __cplusplus
}
#endif
