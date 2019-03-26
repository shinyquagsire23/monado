// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan client side glue to compositor implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_client
 */

#include <stdio.h>
#include <stdlib.h>

#include "util/u_misc.h"

#include "comp_vk_client.h"


/*
 *
 * Swapchain function.
 *
 */

static void
client_vk_swapchain_destroy(struct xrt_swapchain *xsc)
{
	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);
	struct client_vk_compositor *c = sc->c;

	for (uint32_t i = 0; i < sc->base.base.num_images; i++) {
		if (sc->base.images[i] != NULL) {
			c->vk.vkDestroyImage(c->vk.device, sc->base.images[i],
			                     NULL);
			sc->base.images[i] = NULL;
		}

		if (sc->base.mems[i] != NULL) {
			c->vk.vkFreeMemory(c->vk.device, sc->base.mems[i],
			                   NULL);
			sc->base.mems[i] = NULL;
		}
	}

	// Destroy the fd swapchain as well.
	sc->xscfd->base.destroy(&sc->xscfd->base);

	free(sc);
}

static bool
client_vk_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *index)
{
	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return sc->xscfd->base.acquire_image(&sc->xscfd->base, index);
}

static bool
client_vk_swapchain_wait_image(struct xrt_swapchain *xsc,
                               uint64_t timeout,
                               uint32_t index)
{
	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return sc->xscfd->base.wait_image(&sc->xscfd->base, timeout, index);
}

static bool
client_vk_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return sc->xscfd->base.release_image(&sc->xscfd->base, index);
}


/*
 *
 * Compositor functions.
 *
 */

static void
client_vk_compositor_destroy(struct xrt_compositor *xc)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	if (c->vk.cmd_pool != VK_NULL_HANDLE) {
		c->vk.vkDestroyCommandPool(c->vk.device, c->vk.cmd_pool, NULL);
		c->vk.cmd_pool = VK_NULL_HANDLE;
	}

	// Pipe down call into fd compositor.
	c->xcfd->base.destroy(&c->xcfd->base);
	free(c);
}

static void
client_vk_compositor_begin_session(struct xrt_compositor *xc,
                                   enum xrt_view_type type)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	// Pipe down call into fd compositor.
	c->xcfd->base.begin_session(&c->xcfd->base, type);
}

static void
client_vk_compositor_end_session(struct xrt_compositor *xc)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	// Pipe down call into fd compositor.
	c->xcfd->base.end_session(&c->xcfd->base);
}

static void
client_vk_compositor_wait_frame(struct xrt_compositor *xc,
                                int64_t *predicted_display_time,
                                int64_t *predicted_display_period)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	// Pipe down call into fd compositor.
	c->xcfd->base.wait_frame(&c->xcfd->base, predicted_display_time,
	                         predicted_display_period);
}

static void
client_vk_compositor_begin_frame(struct xrt_compositor *xc)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	// Pipe down call into fd compositor.
	c->xcfd->base.begin_frame(&c->xcfd->base);
}

static void
client_vk_compositor_discard_frame(struct xrt_compositor *xc)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	// Pipe down call into fd compositor.
	c->xcfd->base.discard_frame(&c->xcfd->base);
}

static void
client_vk_compositor_end_frame(struct xrt_compositor *xc,
                               enum xrt_blend_mode blend_mode,
                               struct xrt_swapchain **xscs,
                               uint32_t *acquired_index,
                               uint32_t num_swapchains)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct xrt_swapchain *internal[8];

	if (num_swapchains > 8) {
		fprintf(stderr, "ERROR! %s\n", __func__);
		return;
	}

	for (uint32_t i = 0; i < num_swapchains; i++) {
		struct client_vk_swapchain *sc = client_vk_swapchain(xscs[i]);
		internal[i] = &sc->xscfd->base;
	}

	// Pipe down call into fd compositor.
	c->xcfd->base.end_frame(&c->xcfd->base, blend_mode, internal,
	                        acquired_index, num_swapchains);
}

static struct xrt_swapchain *
client_vk_swapchain_create(struct xrt_compositor *xc,
                           enum xrt_swapchain_create_flags create,
                           enum xrt_swapchain_usage_bits bits,
                           int64_t format,
                           uint32_t sample_count,
                           uint32_t width,
                           uint32_t height,
                           uint32_t face_count,
                           uint32_t array_size,
                           uint32_t mip_count)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	VkCommandBuffer cmd_buffer;
	uint32_t num_images = 3;
	VkResult ret;

	struct xrt_swapchain *xsc = c->xcfd->base.create_swapchain(
	    &c->xcfd->base, create, bits, format, sample_count, width, height,
	    face_count, array_size, mip_count);

	if (xsc == NULL) {
		return NULL;
	}

	ret = vk_init_cmd_buffer(&c->vk, &cmd_buffer);
	if (ret != VK_SUCCESS) {
		return NULL;
	}

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = 1,
	};

	struct client_vk_swapchain *sc =
	    U_TYPED_CALLOC(struct client_vk_swapchain);
	sc->base.base.destroy = client_vk_swapchain_destroy;
	sc->base.base.acquire_image = client_vk_swapchain_acquire_image;
	sc->base.base.wait_image = client_vk_swapchain_wait_image;
	sc->base.base.release_image = client_vk_swapchain_release_image;
	sc->base.base.num_images = num_images;
	sc->c = c;
	sc->xscfd = xrt_swapchain_fd(xsc);

	for (uint32_t i = 0; i < num_images; i++) {
		ret = vk_create_image_from_fd(&c->vk, format, width, height,
		                              mip_count, &sc->xscfd->images[i],
		                              &sc->base.images[i],
		                              &sc->base.mems[i]);
		if (ret != VK_SUCCESS) {
			return NULL;
		}

		vk_set_image_layout(&c->vk, cmd_buffer, sc->base.images[i], 0,
		                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                    VK_IMAGE_LAYOUT_UNDEFINED,
		                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                    subresource_range);
	}

	ret = vk_submit_cmd_buffer(&c->vk, cmd_buffer);
	if (ret != VK_SUCCESS) {
		return NULL;
	}

	return &sc->base.base;
}

struct client_vk_compositor *
client_vk_compositor_create(struct xrt_compositor_fd *xcfd,
                            VkInstance instance,
                            PFN_vkGetInstanceProcAddr getProc,
                            VkPhysicalDevice physicalDevice,
                            VkDevice device,
                            uint32_t queueFamilyIndex,
                            uint32_t queueIndex)
{
	VkResult ret;
	struct client_vk_compositor *c =
	    U_TYPED_CALLOC(struct client_vk_compositor);

	c->base.base.create_swapchain = client_vk_swapchain_create;
	c->base.base.begin_session = client_vk_compositor_begin_session;
	c->base.base.end_session = client_vk_compositor_end_session;
	c->base.base.wait_frame = client_vk_compositor_wait_frame;
	c->base.base.begin_frame = client_vk_compositor_begin_frame;
	c->base.base.discard_frame = client_vk_compositor_discard_frame;
	c->base.base.end_frame = client_vk_compositor_end_frame;
	c->base.base.destroy = client_vk_compositor_destroy;

	c->base.base.formats[0] = VK_FORMAT_B8G8R8A8_SRGB;
	c->base.base.formats[1] = VK_FORMAT_R8G8B8A8_SRGB;
	c->base.base.formats[2] = VK_FORMAT_B8G8R8A8_UNORM;
	c->base.base.formats[3] = VK_FORMAT_R8G8B8A8_UNORM;
	c->base.base.num_formats = 4;

	c->xcfd = xcfd;

	ret = vk_init_from_given(&c->vk, getProc, instance, physicalDevice,
	                         device, queueFamilyIndex, queueIndex);
	if (ret != VK_SUCCESS) {
		goto err_free;
	}

	return c;

err_free:
	free(c);
	return NULL;
}
