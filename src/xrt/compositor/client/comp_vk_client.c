// Copyright 2019-2020, Collabora, Ltd.
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
		if (sc->base.images[i] != VK_NULL_HANDLE) {
			c->vk.vkDestroyImage(c->vk.device, sc->base.images[i],
			                     NULL);
			sc->base.images[i] = VK_NULL_HANDLE;
		}

		if (sc->base.mems[i] != VK_NULL_HANDLE) {
			c->vk.vkFreeMemory(c->vk.device, sc->base.mems[i],
			                   NULL);
			sc->base.mems[i] = VK_NULL_HANDLE;
		}
	}

	// Destroy the fd swapchain as well.
	xrt_swapchain_destroy((struct xrt_swapchain **)&sc->xscfd);

	free(sc);
}

static xrt_result_t
client_vk_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *index)
{
	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return xrt_swapchain_acquire_image(&sc->xscfd->base, index);
}

static xrt_result_t
client_vk_swapchain_wait_image(struct xrt_swapchain *xsc,
                               uint64_t timeout,
                               uint32_t index)
{
	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return xrt_swapchain_wait_image(&sc->xscfd->base, timeout, index);
}

static xrt_result_t
client_vk_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return xrt_swapchain_release_image(&sc->xscfd->base, index);
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
	xrt_comp_fd_destroy(&c->xcfd);
	free(c);
}

static xrt_result_t
client_vk_compositor_begin_session(struct xrt_compositor *xc,
                                   enum xrt_view_type type)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	// Pipe down call into fd compositor.
	return xrt_comp_begin_session(&c->xcfd->base, type);
}

static xrt_result_t
client_vk_compositor_end_session(struct xrt_compositor *xc)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	// Pipe down call into fd compositor.
	return xrt_comp_end_session(&c->xcfd->base);
}

static xrt_result_t
client_vk_compositor_wait_frame(struct xrt_compositor *xc,
                                uint64_t *predicted_display_time,
                                uint64_t *predicted_display_period)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	// Pipe down call into fd compositor.
	return xrt_comp_wait_frame(&c->xcfd->base, predicted_display_time,
	                           predicted_display_period);
}

static xrt_result_t
client_vk_compositor_begin_frame(struct xrt_compositor *xc)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	// Pipe down call into fd compositor.
	return xrt_comp_begin_frame(&c->xcfd->base);
}

static xrt_result_t
client_vk_compositor_discard_frame(struct xrt_compositor *xc)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	// Pipe down call into fd compositor.
	return xrt_comp_discard_frame(&c->xcfd->base);
}

static xrt_result_t
client_vk_compositor_layer_begin(struct xrt_compositor *xc,
                                 enum xrt_blend_mode env_blend_mode)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	return xrt_comp_layer_begin(&c->xcfd->base, env_blend_mode);
}

static xrt_result_t
client_vk_compositor_layer_stereo_projection(
    struct xrt_compositor *xc,
    uint64_t timestamp,
    struct xrt_device *xdev,
    enum xrt_input_name name,
    enum xrt_layer_composition_flags layer_flags,
    struct xrt_swapchain *l_sc,
    uint32_t l_image_index,
    struct xrt_rect *l_rect,
    uint32_t l_array_index,
    struct xrt_fov *l_fov,
    struct xrt_pose *l_pose,
    struct xrt_swapchain *r_sc,
    uint32_t r_image_index,
    struct xrt_rect *r_rect,
    uint32_t r_array_index,
    struct xrt_fov *r_fov,
    struct xrt_pose *r_pose,
    bool flip_y)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct xrt_swapchain *l_xscfd, *r_xscfd;

	l_xscfd = &client_vk_swapchain(l_sc)->xscfd->base;
	r_xscfd = &client_vk_swapchain(r_sc)->xscfd->base;

	return xrt_comp_layer_stereo_projection(
	    &c->xcfd->base, timestamp, xdev, name, layer_flags, l_xscfd,
	    l_image_index, l_rect, l_array_index, l_fov, l_pose, r_xscfd,
	    r_image_index, r_rect, r_array_index, r_fov, r_pose, false);
}

static xrt_result_t
client_vk_compositor_layer_quad(struct xrt_compositor *xc,
                                uint64_t timestamp,
                                struct xrt_device *xdev,
                                enum xrt_input_name name,
                                enum xrt_layer_composition_flags layer_flags,
                                enum xrt_layer_eye_visibility visibility,
                                struct xrt_swapchain *sc,
                                uint32_t image_index,
                                struct xrt_rect *rect,
                                uint32_t array_index,
                                struct xrt_pose *pose,
                                struct xrt_vec2 *size,
                                bool flip_y)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct xrt_swapchain *xscfb;

	xscfb = &client_vk_swapchain(sc)->xscfd->base;

	return xrt_comp_layer_quad(&c->xcfd->base, timestamp, xdev, name,
	                           layer_flags, visibility, xscfb, image_index,
	                           rect, array_index, pose, size, false);
}

static xrt_result_t
client_vk_compositor_layer_commit(struct xrt_compositor *xc)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	return xrt_comp_layer_commit(&c->xcfd->base);
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
	VkResult ret;

	struct xrt_swapchain_fd *xscfd = xrt_comp_fd_create_swapchain(
	    c->xcfd, create, bits, format, sample_count, width, height,
	    face_count, array_size, mip_count);

	if (xscfd == NULL) {
		return NULL;
	}
	struct xrt_swapchain *xsc = &xscfd->base;

	ret = vk_init_cmd_buffer(&c->vk, &cmd_buffer);
	if (ret != VK_SUCCESS) {
		return NULL;
	}

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = array_size,
	};

	struct client_vk_swapchain *sc =
	    U_TYPED_CALLOC(struct client_vk_swapchain);
	sc->base.base.destroy = client_vk_swapchain_destroy;
	sc->base.base.acquire_image = client_vk_swapchain_acquire_image;
	sc->base.base.wait_image = client_vk_swapchain_wait_image;
	sc->base.base.release_image = client_vk_swapchain_release_image;
	// Fetch the number of images from the fd swapchain.
	sc->base.base.num_images = xsc->num_images;
	sc->c = c;
	sc->xscfd = xscfd;

	for (uint32_t i = 0; i < xsc->num_images; i++) {
		ret = vk_create_image_from_fd(
		    &c->vk, bits, format, width, height, array_size, mip_count,
		    &xscfd->images[i], &sc->base.images[i], &sc->base.mems[i]);

		// We have consumed this fd now, make sure it's not freed again.
		xscfd->images[i].fd = -1;

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
	c->base.base.layer_begin = client_vk_compositor_layer_begin;
	c->base.base.layer_stereo_projection =
	    client_vk_compositor_layer_stereo_projection;
	c->base.base.layer_quad = client_vk_compositor_layer_quad;
	c->base.base.layer_commit = client_vk_compositor_layer_commit;
	c->base.base.destroy = client_vk_compositor_destroy;
	c->xcfd = xcfd;
	// passthrough our formats from the fd compositor to the client
	for (uint32_t i = 0; i < xcfd->base.num_formats; i++) {
		c->base.base.formats[i] = xcfd->base.formats[i];
	}

	c->base.base.num_formats = xcfd->base.num_formats;

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
