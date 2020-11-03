// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan swapchain code header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "vk/vk_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Structs.
 *
 */

/*!
 * Callback when a @ref vk_swapchain changes size.
 *
 * @ingroup comp_main
 */
typedef void (*vk_swapchain_cb)(uint32_t width, uint32_t height, void *priv);

/*!
 * A pair of VkImage and VkImageView.
 *
 * @ingroup comp_main
 */
struct vk_swapchain_buffer
{
	VkImage image;
	VkImageView view;
};

/*!
 * Wraps and manage VkSwapchainKHR and VkSurfaceKHR, used by @ref comp code.
 *
 * @ingroup comp_main
 */
struct vk_swapchain
{
	struct vk_bundle *vk;

	struct
	{
		VkSwapchainKHR handle;
	} swapchain;

	struct
	{
		VkSurfaceKHR handle;
		VkSurfaceFormatKHR format;
	} surface;

	struct vk_swapchain_buffer *buffers;
	uint32_t image_count;

	struct
	{
		VkFormat color_format;
		VkColorSpaceKHR color_space;
	} preferred;

	//! Present mode that the system must support.
	VkPresentModeKHR present_mode;

	void *cb_priv;
	vk_swapchain_cb dimension_cb;
};


/*
 *
 * Functions.
 *
 */

/*!
 * Wraps and manage VkSwapchainKHR and VkSurfaceKHR, used by @ref comp code.
 *
 * @ingroup comp_main
 */
void
vk_swapchain_init(struct vk_swapchain *sc,
                  struct vk_bundle *vk,
                  vk_swapchain_cb dimension_cb,
                  void *priv);

/*!
 * Initialize the given @ref vk_swapchain, does not allocate.
 *
 * @ingroup comp_main
 */
void
vk_swapchain_create(struct vk_swapchain *sc,
                    uint32_t width,
                    uint32_t height,
                    VkFormat color_format,
                    VkColorSpaceKHR color_space,
                    VkPresentModeKHR present_mode);

/*!
 * Acquire a image index from the given @ref vk_swapchain for rendering.
 *
 * @ingroup comp_main
 */
VkResult
vk_swapchain_acquire_next_image(struct vk_swapchain *sc,
                                VkSemaphore semaphore,
                                uint32_t *index);

/*!
 * Make the given @ref vk_swapchain present the next acquired image.
 *
 * @ingroup comp_main
 */
VkResult
vk_swapchain_present(struct vk_swapchain *sc,
                     VkQueue queue,
                     uint32_t index,
                     VkSemaphore semaphore);

/*!
 * Free all managed resources on the given @ref vk_swapchain,
 * does not free the struct itself.
 *
 * @ingroup comp_main
 */
void
vk_swapchain_cleanup(struct vk_swapchain *sc);


#ifdef __cplusplus
}
#endif
