// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan client side glue to compositor implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_client
 */

#include "util/u_misc.h"

#include "comp_vk_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

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
			c->vk.vkDestroyImage(c->vk.device, sc->base.images[i], NULL);
			sc->base.images[i] = VK_NULL_HANDLE;
		}

		if (sc->mems[i] != VK_NULL_HANDLE) {
			c->vk.vkFreeMemory(c->vk.device, sc->mems[i], NULL);
			sc->mems[i] = VK_NULL_HANDLE;
		}
	}

	// Destroy the native swapchain as well.
	xrt_swapchain_destroy((struct xrt_swapchain **)&sc->xscn);

	free(sc);
}

static xrt_result_t
client_vk_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *out_index)
{
	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);
	struct vk_bundle *vk = &sc->c->vk;

	// Pipe down call into native swapchain.
	xrt_result_t xret = xrt_swapchain_acquire_image(&sc->xscn->base, out_index);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	// Acquire ownership and complete layout transition
	VkSubmitInfo submitInfo = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &sc->acquire[*out_index],
	};

	VkResult ret = vk_locked_submit(vk, vk->queue, 1, &submitInfo, VK_NULL_HANDLE);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "Could not submit to queue: %d", ret);
		return XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS;
	}
	return XRT_SUCCESS;
}

static xrt_result_t
client_vk_swapchain_wait_image(struct xrt_swapchain *xsc, uint64_t timeout, uint32_t index)
{
	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);

	// Pipe down call into native swapchain.
	return xrt_swapchain_wait_image(&sc->xscn->base, timeout, index);
}

static xrt_result_t
client_vk_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);
	struct vk_bundle *vk = &sc->c->vk;

	// Release ownership and begin layout transition
	VkSubmitInfo submitInfo = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &sc->release[index],
	};

	VkResult ret = vk_locked_submit(vk, vk->queue, 1, &submitInfo, VK_NULL_HANDLE);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "Could not submit to queue: %d", ret);
		return XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS;
	}

	// Pipe down call into native swapchain.
	return xrt_swapchain_release_image(&sc->xscn->base, index);
}


/*
 *
 * Compositor functions.
 *
 */

static xrt_result_t
client_vk_compositor_poll_events(struct xrt_compositor *xc, union xrt_compositor_event *out_xce)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_poll_events(&c->xcn->base, out_xce);
}

static void
client_vk_compositor_destroy(struct xrt_compositor *xc)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	if (c->vk.cmd_pool != VK_NULL_HANDLE) {
		// Make sure that any of the command buffers from this command
		// pool are n used here, this pleases the validation layer.
		os_mutex_lock(&c->vk.queue_mutex);
		c->vk.vkDeviceWaitIdle(c->vk.device);
		os_mutex_unlock(&c->vk.queue_mutex);

		c->vk.vkDestroyCommandPool(c->vk.device, c->vk.cmd_pool, NULL);
		c->vk.cmd_pool = VK_NULL_HANDLE;
	}

	free(c);
}

static xrt_result_t
client_vk_compositor_begin_session(struct xrt_compositor *xc, enum xrt_view_type type)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_begin_session(&c->xcn->base, type);
}

static xrt_result_t
client_vk_compositor_end_session(struct xrt_compositor *xc)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_end_session(&c->xcn->base);
}

static xrt_result_t
client_vk_compositor_wait_frame(struct xrt_compositor *xc,
                                int64_t *out_frame_id,
                                uint64_t *predicted_display_time,
                                uint64_t *predicted_display_period)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_wait_frame(&c->xcn->base, out_frame_id, predicted_display_time, predicted_display_period);
}

static xrt_result_t
client_vk_compositor_begin_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_begin_frame(&c->xcn->base, frame_id);
}

static xrt_result_t
client_vk_compositor_discard_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_discard_frame(&c->xcn->base, frame_id);
}

static xrt_result_t
client_vk_compositor_layer_begin(struct xrt_compositor *xc, int64_t frame_id, enum xrt_blend_mode env_blend_mode)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	return xrt_comp_layer_begin(&c->xcn->base, frame_id, env_blend_mode);
}

static xrt_result_t
client_vk_compositor_layer_stereo_projection(struct xrt_compositor *xc,
                                             struct xrt_device *xdev,
                                             struct xrt_swapchain *l_xsc,
                                             struct xrt_swapchain *r_xsc,
                                             const struct xrt_layer_data *data)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct xrt_swapchain *l_xscn, *r_xscn;

	assert(data->type == XRT_LAYER_STEREO_PROJECTION);

	l_xscn = &client_vk_swapchain(l_xsc)->xscn->base;
	r_xscn = &client_vk_swapchain(r_xsc)->xscn->base;

	return xrt_comp_layer_stereo_projection(&c->xcn->base, xdev, l_xscn, r_xscn, data);
}


static xrt_result_t
client_vk_compositor_layer_stereo_projection_depth(struct xrt_compositor *xc,
                                                   struct xrt_device *xdev,
                                                   struct xrt_swapchain *l_xsc,
                                                   struct xrt_swapchain *r_xsc,
                                                   struct xrt_swapchain *l_d_xsc,
                                                   struct xrt_swapchain *r_d_xsc,
                                                   const struct xrt_layer_data *data)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct xrt_swapchain *l_xscn, *r_xscn, *l_d_xscn, *r_d_xscn;

	assert(data->type == XRT_LAYER_STEREO_PROJECTION_DEPTH);

	l_xscn = &client_vk_swapchain(l_xsc)->xscn->base;
	r_xscn = &client_vk_swapchain(r_xsc)->xscn->base;
	l_d_xscn = &client_vk_swapchain(l_d_xsc)->xscn->base;
	r_d_xscn = &client_vk_swapchain(r_d_xsc)->xscn->base;

	return xrt_comp_layer_stereo_projection_depth(&c->xcn->base, xdev, l_xscn, r_xscn, l_d_xscn, r_d_xscn, data);
}

static xrt_result_t
client_vk_compositor_layer_quad(struct xrt_compositor *xc,
                                struct xrt_device *xdev,
                                struct xrt_swapchain *xsc,
                                const struct xrt_layer_data *data)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_QUAD);

	xscfb = &client_vk_swapchain(xsc)->xscn->base;

	return xrt_comp_layer_quad(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_vk_compositor_layer_cube(struct xrt_compositor *xc,
                                struct xrt_device *xdev,
                                struct xrt_swapchain *xsc,
                                const struct xrt_layer_data *data)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_CUBE);

	xscfb = &client_vk_swapchain(xsc)->xscn->base;

	return xrt_comp_layer_cube(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_vk_compositor_layer_cylinder(struct xrt_compositor *xc,
                                    struct xrt_device *xdev,
                                    struct xrt_swapchain *xsc,
                                    const struct xrt_layer_data *data)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_CYLINDER);

	xscfb = &client_vk_swapchain(xsc)->xscn->base;

	return xrt_comp_layer_cylinder(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_vk_compositor_layer_equirect1(struct xrt_compositor *xc,
                                     struct xrt_device *xdev,
                                     struct xrt_swapchain *xsc,
                                     const struct xrt_layer_data *data)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_EQUIRECT1);

	xscfb = &client_vk_swapchain(xsc)->xscn->base;

	return xrt_comp_layer_equirect1(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_vk_compositor_layer_equirect2(struct xrt_compositor *xc,
                                     struct xrt_device *xdev,
                                     struct xrt_swapchain *xsc,
                                     const struct xrt_layer_data *data)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_EQUIRECT2);

	xscfb = &client_vk_swapchain(xsc)->xscn->base;

	return xrt_comp_layer_equirect2(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_vk_compositor_layer_commit(struct xrt_compositor *xc, int64_t frame_id, xrt_graphics_sync_handle_t sync_handle)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	//! @todo We should be creating the handle ourselves in the future.
	assert(!xrt_graphics_sync_handle_is_valid(sync_handle));

	return xrt_comp_layer_commit(&c->xcn->base, frame_id, XRT_GRAPHICS_SYNC_HANDLE_INVALID);
}

static xrt_result_t
client_vk_swapchain_create(struct xrt_compositor *xc,
                           const struct xrt_swapchain_create_info *info,
                           struct xrt_swapchain **out_xsc)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);
	VkCommandBuffer cmd_buffer;
	VkResult ret;
	xrt_result_t xret;

	struct xrt_swapchain_native *xscn = NULL;
	xret = xrt_comp_native_create_swapchain(c->xcn, info, &xscn);

	if (xret != XRT_SUCCESS) {
		return xret;
	}
	assert(xscn != NULL);

	struct xrt_swapchain *xsc = &xscn->base;

	ret = vk_init_cmd_buffer(&c->vk, &cmd_buffer);
	if (ret != VK_SUCCESS) {
		return XRT_ERROR_VULKAN;
	}

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = VK_REMAINING_MIP_LEVELS,
	    .baseArrayLayer = 0,
	    .layerCount = VK_REMAINING_ARRAY_LAYERS,
	};

	struct client_vk_swapchain *sc = U_TYPED_CALLOC(struct client_vk_swapchain);
	sc->base.base.destroy = client_vk_swapchain_destroy;
	sc->base.base.acquire_image = client_vk_swapchain_acquire_image;
	sc->base.base.wait_image = client_vk_swapchain_wait_image;
	sc->base.base.release_image = client_vk_swapchain_release_image;
	// Fetch the number of images from the native swapchain.
	sc->base.base.num_images = xsc->num_images;
	sc->c = c;
	sc->xscn = xscn;

	for (uint32_t i = 0; i < xsc->num_images; i++) {
		ret = vk_create_image_from_native(&c->vk, info, &xscn->images[i], &sc->base.images[i], &sc->mems[i]);


		if (ret != VK_SUCCESS) {
			return XRT_ERROR_VULKAN;
		}

		/*
		 * This is only to please the validation layer, that may or may
		 * not be a bug in the validation layer. That may or may not be
		 * fixed in the future version of the validation layer.
		 */
		vk_set_image_layout(&c->vk, cmd_buffer, sc->base.images[i], 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, subresource_range);
	}

	ret = vk_submit_cmd_buffer(&c->vk, cmd_buffer);
	if (ret != VK_SUCCESS) {
		return XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS;
	}

	// Prerecord command buffers for swapchain image ownership/layout
	// transitions
	for (uint32_t i = 0; i < xsc->num_images; i++) {
		ret = vk_init_cmd_buffer(&c->vk, &sc->acquire[i]);
		if (ret != VK_SUCCESS) {
			return XRT_ERROR_VULKAN;
		}
		ret = vk_init_cmd_buffer(&c->vk, &sc->release[i]);
		if (ret != VK_SUCCESS) {
			return XRT_ERROR_VULKAN;
		}

		VkImageSubresourceRange subresource_range = {
		    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		    .baseMipLevel = 0,
		    .levelCount = VK_REMAINING_MIP_LEVELS,
		    .baseArrayLayer = 0,
		    .layerCount = VK_REMAINING_ARRAY_LAYERS,
		};

		/*
		 * The biggest reason is that VK_IMAGE_LAYOUT_PRESENT_SRC_KHR is
		 * used here is that this is what hello_xr used to barrier to,
		 * and it worked on a wide verity of drivers. So it's safe.
		 *
		 * There might not be a Vulkan renderer on the other endm
		 * there could be a OpenGL compositor, heck there could be a X
		 * server even. On Linux VK_IMAGE_LAYOUT_PRESENT_SRC_KHR is what
		 * you use if you want to "flush" out all of the pixels to the
		 * memory buffer that has been shared to you from a X11 server.
		 *
		 * This is not what the spec says you should do when it comes to
		 * external images thou. Instead we should use the queue family
		 * index `VK_QUEUE_FAMILY_EXTERNAL`. And use semaphores to
		 * synchronize.
		 */
		VkImageMemoryBarrier acquire = {
		    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		    .srcAccessMask = 0,
		    .dstAccessMask = vk_swapchain_access_flags(info->bits),
		    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		    .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    .image = sc->base.images[i],
		    .subresourceRange = subresource_range,
		};

		VkImageMemoryBarrier release = {
		    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		    .srcAccessMask = vk_swapchain_access_flags(info->bits),
		    .dstAccessMask = 0,
		    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		    .srcQueueFamilyIndex = c->vk.queue_family_index,
		    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL,
		    .image = sc->base.images[i],
		    .subresourceRange = subresource_range,
		};

		//! @todo less conservative pipeline stage masks based on usage
		c->vk.vkCmdPipelineBarrier(sc->acquire[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &acquire);
		c->vk.vkCmdPipelineBarrier(sc->release[i], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &release);

		ret = c->vk.vkEndCommandBuffer(sc->acquire[i]);
		if (ret != VK_SUCCESS) {
			VK_ERROR((&c->vk), "vkEndCommandBuffer: %s", vk_result_string(ret));
			return XRT_ERROR_VULKAN;
		}
		ret = c->vk.vkEndCommandBuffer(sc->release[i]);
		if (ret != VK_SUCCESS) {
			VK_ERROR((&c->vk), "vkEndCommandBuffer: %s", vk_result_string(ret));
			return XRT_ERROR_VULKAN;
		}
	}

	*out_xsc = &sc->base.base;

	return XRT_SUCCESS;
}

struct client_vk_compositor *
client_vk_compositor_create(struct xrt_compositor_native *xcn,
                            VkInstance instance,
                            PFN_vkGetInstanceProcAddr getProc,
                            VkPhysicalDevice physicalDevice,
                            VkDevice device,
                            uint32_t queueFamilyIndex,
                            uint32_t queueIndex)
{
	VkResult ret;
	struct client_vk_compositor *c = U_TYPED_CALLOC(struct client_vk_compositor);

	c->base.base.create_swapchain = client_vk_swapchain_create;
	c->base.base.begin_session = client_vk_compositor_begin_session;
	c->base.base.end_session = client_vk_compositor_end_session;
	c->base.base.wait_frame = client_vk_compositor_wait_frame;
	c->base.base.begin_frame = client_vk_compositor_begin_frame;
	c->base.base.discard_frame = client_vk_compositor_discard_frame;
	c->base.base.layer_begin = client_vk_compositor_layer_begin;
	c->base.base.layer_stereo_projection = client_vk_compositor_layer_stereo_projection;
	c->base.base.layer_stereo_projection_depth = client_vk_compositor_layer_stereo_projection_depth;
	c->base.base.layer_quad = client_vk_compositor_layer_quad;
	c->base.base.layer_cube = client_vk_compositor_layer_cube;
	c->base.base.layer_cylinder = client_vk_compositor_layer_cylinder;
	c->base.base.layer_equirect1 = client_vk_compositor_layer_equirect1;
	c->base.base.layer_equirect2 = client_vk_compositor_layer_equirect2;
	c->base.base.layer_commit = client_vk_compositor_layer_commit;
	c->base.base.destroy = client_vk_compositor_destroy;
	c->base.base.poll_events = client_vk_compositor_poll_events;

	c->xcn = xcn;
	// passthrough our formats from the native compositor to the client
	for (uint32_t i = 0; i < xcn->base.info.num_formats; i++) {
		c->base.base.info.formats[i] = xcn->base.info.formats[i];
	}

	c->base.base.info.num_formats = xcn->base.info.num_formats;

	ret = vk_init_from_given(&c->vk, getProc, instance, physicalDevice, device, queueFamilyIndex, queueIndex);
	if (ret != VK_SUCCESS) {
		goto err_free;
	}

	return c;

err_free:
	free(c);
	return NULL;
}
