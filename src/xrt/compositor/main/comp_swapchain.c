// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Swapchain code for the main compositor.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include "util/u_misc.h"

#include "main/comp_compositor.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


static void
swapchain_destroy(struct xrt_swapchain *xsc)
{
	struct comp_swapchain *sc = comp_swapchain(xsc);

	COMP_SPEW(sc->c, "DESTROY");

	u_threading_stack_push(&sc->c->threading.destroy_swapchains, sc);
}

static xrt_result_t
swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *out_index)
{
	struct comp_swapchain *sc = comp_swapchain(xsc);

	COMP_SPEW(sc->c, "ACQUIRE_IMAGE");

	// Returns negative on empty fifo.
	int res = u_index_fifo_pop(&sc->fifo, out_index);
	if (res >= 0) {
		return XRT_SUCCESS;
	} else {
		return XRT_ERROR_NO_IMAGE_AVAILABLE;
	}
}

static xrt_result_t
swapchain_wait_image(struct xrt_swapchain *xsc,
                     uint64_t timeout,
                     uint32_t index)
{
	struct comp_swapchain *sc = comp_swapchain(xsc);

	COMP_SPEW(sc->c, "WAIT_IMAGE");
	return XRT_SUCCESS;
}

static xrt_result_t
swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	struct comp_swapchain *sc = comp_swapchain(xsc);

	COMP_SPEW(sc->c, "RELEASE_IMAGE");

	int res = u_index_fifo_push(&sc->fifo, index);

	if (res >= 0) {
		return XRT_SUCCESS;
	} else {
		// FIFO full
		return XRT_ERROR_NO_IMAGE_AVAILABLE;
	}
}

static VkResult
get_device_memory_fd(struct comp_compositor *c,
                     VkDeviceMemory device_memory,
                     int *out_fd)
{

	// vkGetMemoryFdKHR parameter
	VkMemoryGetFdInfoKHR fd_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
	    .memory = device_memory,
	    .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
	};
	int fd;
	VkResult ret = c->vk.vkGetMemoryFdKHR(c->vk.device, &fd_info, &fd);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "->image - vkGetMemoryFdKHR: %s",
		           vk_result_string(ret));
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}
	*out_fd = fd;
	return ret;
}

static VkResult
create_image_fd(struct comp_compositor *c,
                enum xrt_swapchain_usage_bits swapchain_usage,
                int64_t format,
                uint32_t width,
                uint32_t height,
                uint32_t array_size,
                uint32_t mip_count,
                VkImage *out_image,
                VkDeviceMemory *out_mem,
                struct xrt_image_fd *out_image_fd)
{
	VkImageUsageFlags image_usage =
	    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	VkDeviceMemory device_memory = VK_NULL_HANDLE;
	VkImage image = VK_NULL_HANDLE;
	VkResult ret = VK_SUCCESS;
	VkDeviceSize size;
	int fd;

	COMP_SPEW(c, "->image - vkCreateImage %dx%d", width, height);


	/*
	 * Create the image.
	 */

	VkExternalMemoryImageCreateInfoKHR external_memory_image_create_info = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR,
	    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
	};

	if ((swapchain_usage & XRT_SWAPCHAIN_USAGE_COLOR) != 0) {
		image_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if ((swapchain_usage & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0) {
		image_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	if ((swapchain_usage & XRT_SWAPCHAIN_USAGE_UNORDERED_ACCESS) != 0) {
		image_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	if ((swapchain_usage & XRT_SWAPCHAIN_USAGE_TRANSFER_SRC) != 0) {
		image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if ((swapchain_usage & XRT_SWAPCHAIN_USAGE_TRANSFER_DST) != 0) {
		image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}
	if ((swapchain_usage & XRT_SWAPCHAIN_USAGE_SAMPLED) != 0) {
		image_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}

	VkImageCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .pNext = &external_memory_image_create_info,
	    .imageType = VK_IMAGE_TYPE_2D,
	    .format = (VkFormat)format,
	    .extent = {.width = width, .height = height, .depth = 1},
	    .mipLevels = mip_count,
	    .arrayLayers = array_size,
	    .samples = VK_SAMPLE_COUNT_1_BIT,
	    .tiling = VK_IMAGE_TILING_OPTIMAL,
	    .usage = image_usage,
	    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	ret = c->vk.vkCreateImage(c->vk.device, &info, NULL, &image);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "->image - vkCreateImage: %s",
		           vk_result_string(ret));
		// Nothing to cleanup
		return ret;
	}

	/*
	 * Create and bind the memory.
	 */
	// vkAllocateMemory parameters
	VkMemoryDedicatedAllocateInfoKHR dedicated_memory_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR,
	    .image = image,
	    .buffer = VK_NULL_HANDLE,
	};

	VkExportMemoryAllocateInfo export_alloc_info = {
	    .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR,
	    .pNext = &dedicated_memory_info,
	    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
	};

	ret = vk_alloc_and_bind_image_memory(
	    &c->vk, image, SIZE_MAX, &export_alloc_info, &device_memory, &size);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "->image - vkAllocateMemory: %s",
		           vk_result_string(ret));
		goto err_image;
	}

	/*
	 * Get the fd.
	 */
	ret = get_device_memory_fd(c, device_memory, &fd);
	if (ret != VK_SUCCESS) {
		goto err_mem;
	}


	*out_image = image;
	*out_mem = device_memory;
	out_image_fd->fd = fd;
	out_image_fd->size = size;

	return ret;

err_mem:
	c->vk.vkFreeMemory(c->vk.device, device_memory, NULL);
err_image:
	c->vk.vkDestroyImage(c->vk.device, image, NULL);
	return ret;
}


/*
 *
 * Exported functions.
 *
 */

struct xrt_swapchain *
comp_swapchain_create(struct xrt_compositor *xc,
                      struct xrt_swapchain_create_info *info)
{
	struct comp_compositor *c = comp_compositor(xc);
	VkCommandBuffer cmd_buffer;
	uint32_t num_images = 3;
	VkResult ret;


	if ((info->create & XRT_SWAPCHAIN_CREATE_STATIC_IMAGE) != 0) {
		num_images = 1;
	}

	struct comp_swapchain *sc = U_TYPED_CALLOC(struct comp_swapchain);
	sc->base.base.destroy = swapchain_destroy;
	sc->base.base.acquire_image = swapchain_acquire_image;
	sc->base.base.wait_image = swapchain_wait_image;
	sc->base.base.release_image = swapchain_release_image;
	sc->base.base.num_images = num_images;
	sc->c = c;

	COMP_DEBUG(c, "CREATE %p %dx%d", (void *)sc, info->width, info->height);

	// Make sure the fds are invalid.
	for (uint32_t i = 0; i < ARRAY_SIZE(sc->base.images); i++) {
		sc->base.images[i].fd = -1;
	}

	for (uint32_t i = 0; i < num_images; i++) {
		ret = create_image_fd(
		    c, info->bits, info->format, info->width, info->height,
		    info->array_size, info->mip_count, &sc->images[i].image,
		    &sc->images[i].memory, &sc->base.images[i]);
		if (ret != VK_SUCCESS) {
			//! @todo memory leak of image fds and swapchain
			// see
			// https://gitlab.freedesktop.org/monado/monado/issues/20
			return NULL;
		}

		vk_create_sampler(&c->vk, &sc->images[i].sampler);
	}

	VkComponentMapping components = {
	    .r = VK_COMPONENT_SWIZZLE_R,
	    .g = VK_COMPONENT_SWIZZLE_G,
	    .b = VK_COMPONENT_SWIZZLE_B,
	    .a = VK_COMPONENT_SWIZZLE_ONE,
	};

	for (uint32_t i = 0; i < num_images; i++) {
		sc->images[i].views.alpha =
		    U_TYPED_ARRAY_CALLOC(VkImageView, info->array_size);
		sc->images[i].views.no_alpha =
		    U_TYPED_ARRAY_CALLOC(VkImageView, info->array_size);
		sc->images[i].array_size = info->array_size;

		for (uint32_t layer = 0; layer < info->array_size; ++layer) {
			VkImageSubresourceRange subresource_range = {
			    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			    .baseMipLevel = 0,
			    .levelCount = 1,
			    .baseArrayLayer = layer,
			    .layerCount = 1,
			};


			vk_create_view(&c->vk, sc->images[i].image,
			               (VkFormat)info->format,
			               subresource_range,
			               &sc->images[i].views.alpha[layer]);
			vk_create_view_swizzle(
			    &c->vk, sc->images[i].image, (VkFormat)info->format,
			    subresource_range, components,
			    &sc->images[i].views.no_alpha[layer]);
		}
	}

	// Prime the fifo
	for (uint32_t i = 0; i < num_images; i++) {
		u_index_fifo_push(&sc->fifo, i);
	}


	/*
	 *
	 * Transition image.
	 *
	 */

	vk_init_cmd_buffer(&c->vk, &cmd_buffer);

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = info->array_size,
	};

	for (uint32_t i = 0; i < num_images; i++) {
		vk_set_image_layout(&c->vk, cmd_buffer, sc->images[i].image, 0,
		                    VK_ACCESS_SHADER_READ_BIT,
		                    VK_IMAGE_LAYOUT_UNDEFINED,
		                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                    subresource_range);
	}

	vk_submit_cmd_buffer(&c->vk, cmd_buffer);

	return &sc->base.base;
}

static void
clean_image_views(struct vk_bundle *vk,
                  size_t array_size,
                  VkImageView **views_ptr)
{
	VkImageView *views = *views_ptr;
	if (views == NULL) {
		return;
	}

	for (uint32_t i = 0; i < array_size; ++i) {
		if (views[i] == VK_NULL_HANDLE) {
			continue;
		}

		vk->vkDestroyImageView(vk->device, views[i], NULL);
		views[i] = VK_NULL_HANDLE;
	}

	free(views);
	array_size = 0;

	*views_ptr = NULL;
}

/*!
 * Free and destroy any initialized fields on the given image, safe to pass in
 * images that has one or all fields set to NULL.
 */
static void
comp_swapchain_image_cleanup(struct vk_bundle *vk,
                             struct comp_swapchain_image *image)
{
	vk->vkDeviceWaitIdle(vk->device);

	clean_image_views(vk, image->array_size, &image->views.alpha);
	clean_image_views(vk, image->array_size, &image->views.no_alpha);

	if (image->sampler != VK_NULL_HANDLE) {
		vk->vkDestroySampler(vk->device, image->sampler, NULL);
		image->sampler = VK_NULL_HANDLE;
	}

	if (image->image != VK_NULL_HANDLE) {
		vk->vkDestroyImage(vk->device, image->image, NULL);
		image->image = VK_NULL_HANDLE;
	}

	if (image->memory != VK_NULL_HANDLE) {
		vk->vkFreeMemory(vk->device, image->memory, NULL);
		image->memory = VK_NULL_HANDLE;
	}
}

void
comp_swapchain_really_destroy(struct comp_swapchain *sc)
{
	struct vk_bundle *vk = &sc->c->vk;

	COMP_SPEW(sc->c, "REALLY DESTROY");

	for (uint32_t i = 0; i < sc->base.base.num_images; i++) {
		comp_swapchain_image_cleanup(vk, &sc->images[i]);
	}

	for (uint32_t i = 0; i < sc->base.base.num_images; i++) {
		if (sc->base.images[i].fd < 0) {
			continue;
		}

		close(sc->base.images[i].fd);
		sc->base.images[i].fd = -1;
	}

	free(sc);
}
