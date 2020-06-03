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

	//! Owning reference to the backing fd swapchain.
	struct xrt_swapchain_fd *xscfd;

	//! Non-owning reference to our parent compositor.
	struct client_vk_compositor *c;
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

	//! Owning reference to the backing fd compositor
	struct xrt_compositor_fd *xcfd;

	struct vk_bundle vk;
};


/*
 *
 * Functions and helpers.
 *
 */

/*!
 * Down-cast helper.
 *
 * @private @memberof client_vk_swapchain
 */
static inline struct client_vk_swapchain *
client_vk_swapchain(struct xrt_swapchain *xsc)
{
	return (struct client_vk_swapchain *)xsc;
}

/*!
 * Down-cast helper.
 *
 * @private @memberof client_vk_compositor
 */
static inline struct client_vk_compositor *
client_vk_compositor(struct xrt_compositor *xc)
{
	return (struct client_vk_compositor *)xc;
}

/*!
 * Create a new client_vk_compositor.
 *
 * @public @memberof client_vk_compositor
 * @relatesalso xrt_compositor_fd
 */
struct client_vk_compositor *
client_vk_compositor_create(struct xrt_compositor_fd *xcfd,
                            VkInstance instance,
                            PFN_vkGetInstanceProcAddr getProc,
                            VkPhysicalDevice physicalDevice,
                            VkDevice device,
                            uint32_t queueFamilyIndex,
                            uint32_t queueIndex);


#ifdef __cplusplus
}
#endif
