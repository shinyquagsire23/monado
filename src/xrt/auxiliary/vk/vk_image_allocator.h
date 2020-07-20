// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan image allocator helper.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_vk
 */

#pragma once

#include "xrt/xrt_config_os.h"
#include "xrt/xrt_vulkan_includes.h"
#include "vk/vk_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @ingroup aux_vk
 * @{
 */

struct vk_image
{
	VkImage handle;
	VkDeviceMemory memory;
	VkDeviceSize size;
};

struct vk_image_collection
{
	struct xrt_swapchain_create_info info;

	struct vk_image images[8];

	uint32_t num_images;
};

/*!
 * Allocates image(s) using the information specified int he swapcain create
 * info.
 */
VkResult
vk_ic_allocate(struct vk_bundle *vk,
               const struct xrt_swapchain_create_info *xscci,
               uint32_t num_images,
               struct vk_image_collection *out_vkic);

/*!
 * Imports and set images from the given FDs.
 */
VkResult
vk_ic_from_natives(struct vk_bundle *vk,
                   const struct xrt_swapchain_create_info *xscci,
                   struct xrt_image_native *native_images,
                   uint32_t num_images,
                   struct vk_image_collection *out_vkic);

/*!
 * Free all images created on this image collection, doesn't free the struct
 * itself so the caller needs to ensure that.
 */
void
vk_ic_destroy(struct vk_bundle *vk, struct vk_image_collection *vkic);

/*!
 * Get the native handles (FDs on desktop Linux) for the images, this is a all
 * or nothing function. The ownership is transferred from the images to the
 * caller so it is responsible for them to be closed just like with
 * vkGetMemoryFdKHR.
 *
 * @see
 * https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_memory_fd.html
 */
VkResult
vk_ic_get_handles(struct vk_bundle *vk,
                  struct vk_image_collection *vkic,
                  uint32_t max_handles,
                  xrt_graphics_buffer_handle_t *out_handles);


/*!
 * @}
 */

#ifdef __cplusplus
}
#endif
