// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan client side glue to compositor implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_client
 */

#include "util/u_misc.h"
#include "util/u_trace_marker.h"

#include "comp_vk_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define MS_TO_NS(ms) (ms * 1000L * 1000L)

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
	COMP_TRACE_MARKER();

	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);
	struct client_vk_compositor *c = sc->c;
	struct vk_bundle *vk = &c->vk;

	for (uint32_t i = 0; i < sc->base.base.image_count; i++) {

		VkResult ret = vk->vkWaitForFences(vk->device, 1, &sc->acquire_release_fence[i], true, MS_TO_NS(500));
		if (vk_has_error(ret, "vkWaitForFences", __FILE__, __LINE__)) {
			// don't really care, we are going to destroy anyway, just make sure it's not used anymore
			vk->vkDeviceWaitIdle(vk->device);
		}

		if (sc->base.images[i] != VK_NULL_HANDLE) {
			vk->vkDestroyImage(vk->device, sc->base.images[i], NULL);
			sc->base.images[i] = VK_NULL_HANDLE;
		}

		if (sc->mems[i] != VK_NULL_HANDLE) {
			vk->vkFreeMemory(vk->device, sc->mems[i], NULL);
			sc->mems[i] = VK_NULL_HANDLE;
		}
	}

	// Drop our reference, does NULL checking.
	xrt_swapchain_native_reference(&sc->xscn, NULL);

	free(sc);
}

static xrt_result_t
client_vk_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *out_index)
{
	COMP_TRACE_MARKER();

	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);
	struct vk_bundle *vk = &sc->c->vk;

	// Pipe down call into native swapchain.
	xrt_result_t xret = xrt_swapchain_acquire_image(&sc->xscn->base, out_index);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	VkResult ret;

	ret = vk->vkWaitForFences(vk->device, 1, &sc->acquire_release_fence[*out_index], true, MS_TO_NS(500));
	vk_check_error("vkWaitForFences", ret, XRT_ERROR_VULKAN);

	ret = vk->vkResetFences(vk->device, 1, &sc->acquire_release_fence[*out_index]);
	vk_check_error("vkResetFences", ret, XRT_ERROR_VULKAN);

	// Acquire ownership and complete layout transition
	VkSubmitInfo submitInfo = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &sc->acquire[*out_index],
	};

	ret = vk_locked_submit(vk, vk->queue, 1, &submitInfo, sc->acquire_release_fence[*out_index]);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "Could not submit to queue: %d", ret);
		return XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS;
	}

	return XRT_SUCCESS;
}

static xrt_result_t
client_vk_swapchain_wait_image(struct xrt_swapchain *xsc, uint64_t timeout, uint32_t index)
{
	COMP_TRACE_MARKER();

	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);

	// Pipe down call into native swapchain.
	return xrt_swapchain_wait_image(&sc->xscn->base, timeout, index);
}

static xrt_result_t
client_vk_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	COMP_TRACE_MARKER();

	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);
	struct vk_bundle *vk = &sc->c->vk;

	VkResult ret;

	{
		COMP_TRACE_IDENT("fence");

		ret = vk->vkWaitForFences(vk->device, 1, &sc->acquire_release_fence[index], true, MS_TO_NS(500));
		vk_check_error("vkWaitForFences", ret, XRT_ERROR_VULKAN);

		vk->vkResetFences(vk->device, 1, &sc->acquire_release_fence[index]);
		vk_check_error("vkResetFences", ret, XRT_ERROR_VULKAN);
	}

	{
		COMP_TRACE_IDENT("submit");

		// Release ownership and begin layout transition
		VkSubmitInfo submitInfo = {
		    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		    .commandBufferCount = 1,
		    .pCommandBuffers = &sc->release[index],
		};

		ret = vk_locked_submit(vk, vk->queue, 1, &submitInfo, sc->acquire_release_fence[index]);
		if (ret != VK_SUCCESS) {
			VK_ERROR(vk, "Could not submit to queue: %d", ret);
			return XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS;
		}
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
	COMP_TRACE_MARKER();

	struct client_vk_compositor *c = client_vk_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_poll_events(&c->xcn->base, out_xce);
}

static void
client_vk_compositor_destroy(struct xrt_compositor *xc)
{
	COMP_TRACE_MARKER();

	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct vk_bundle *vk = &c->vk;

	if (vk->cmd_pool != VK_NULL_HANDLE) {
		// Make sure that any of the command buffers from this command
		// pool are n used here, this pleases the validation layer.
		os_mutex_lock(&vk->queue_mutex);
		vk->vkDeviceWaitIdle(vk->device);
		os_mutex_unlock(&vk->queue_mutex);

		vk->vkDestroyCommandPool(vk->device, vk->cmd_pool, NULL);
		vk->cmd_pool = VK_NULL_HANDLE;
	}
	vk_deinit_mutex(vk);

	free(c);
}

static xrt_result_t
client_vk_compositor_begin_session(struct xrt_compositor *xc, enum xrt_view_type type)
{
	COMP_TRACE_MARKER();

	struct client_vk_compositor *c = client_vk_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_begin_session(&c->xcn->base, type);
}

static xrt_result_t
client_vk_compositor_end_session(struct xrt_compositor *xc)
{
	COMP_TRACE_MARKER();

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
	COMP_TRACE_MARKER();

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
client_vk_compositor_layer_begin(struct xrt_compositor *xc,
                                 int64_t frame_id,
                                 uint64_t display_time_ns,
                                 enum xrt_blend_mode env_blend_mode)
{
	COMP_TRACE_MARKER();

	struct client_vk_compositor *c = client_vk_compositor(xc);

	return xrt_comp_layer_begin(&c->xcn->base, frame_id, display_time_ns, env_blend_mode);
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
	COMP_TRACE_MARKER();

	struct client_vk_compositor *c = client_vk_compositor(xc);

	// Got a ready made handle, assume it's in the command stream and call commit directly.
	if (xrt_graphics_sync_handle_is_valid(sync_handle)) {
		// Commit consumes the sync_handle.
		return xrt_comp_layer_commit(&c->xcn->base, frame_id, sync_handle);
	}

	struct vk_bundle *vk = &c->vk;
	VkResult ret = VK_SUCCESS;

#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	// Using sync fds currently to match OpenGL extension.
	bool sync_fence = vk->external.fence_sync_fd;
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	bool sync_fence = vk->external.fence_win32_handle;
#else
#error "Need port to export fence sync handles"
#endif

	/*!
	 * @todo Even with a fence it won't help that much as the multi-
	 * compositor waits on the fence in commit before proceeding. To fix
	 * that a thread will be added in the multi-compositor to wait on the
	 * fence and allow commit to return. Better still is using timeline
	 * semaphores for it all.
	 */
	if (sync_fence) {
		COMP_TRACE_IDENT(create_and_submit_fence);

		ret = vk_create_and_submit_fence_native(vk, &sync_handle);
		if (ret != VK_SUCCESS) {
			// This is really bad, log again.
			U_LOG_E("Could not create and submit a native fence!");
			return XRT_ERROR_VULKAN;
		}
	} else {
		COMP_TRACE_IDENT(device_wait_idle);

		// Last course of action fallback.
		vk->vkDeviceWaitIdle(vk->device);
	}

	// Commit consumes the sync_handle.
	return xrt_comp_layer_commit(&c->xcn->base, frame_id, sync_handle);
}

static xrt_result_t
client_vk_swapchain_create(struct xrt_compositor *xc,
                           const struct xrt_swapchain_create_info *info,
                           struct xrt_swapchain **out_xsc)
{
	COMP_TRACE_MARKER();

	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct vk_bundle *vk = &c->vk;
	VkCommandBuffer cmd_buffer;
	VkResult ret;
	xrt_result_t xret;

	struct xrt_swapchain_native *xscn = NULL; // Has to be NULL.
	xret = xrt_comp_native_create_swapchain(c->xcn, info, &xscn);

	if (xret != XRT_SUCCESS) {
		return xret;
	}
	assert(xscn != NULL);

	struct xrt_swapchain *xsc = &xscn->base;

	ret = vk_init_cmd_buffer(vk, &cmd_buffer);
	if (ret != VK_SUCCESS) {
		return XRT_ERROR_VULKAN;
	}

	VkAccessFlags barrier_access_mask = vk_csci_get_barrier_access_mask(info->bits);
	VkImageLayout barrier_optimal_layout = vk_csci_get_barrier_optimal_layout(info->format);
	VkImageAspectFlags barrier_aspect_mask = vk_csci_get_barrier_aspect_mask(info->format);

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = barrier_aspect_mask,
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
	sc->base.base.reference.count = 1;
	sc->base.base.image_count = xsc->image_count; // Fetch the number of images from the native swapchain.
	sc->c = c;
	sc->xscn = xscn;

	for (uint32_t i = 0; i < xsc->image_count; i++) {
		ret = vk_create_image_from_native(vk, info, &xscn->images[i], &sc->base.images[i], &sc->mems[i]);


		if (ret != VK_SUCCESS) {
			return XRT_ERROR_VULKAN;
		}

		/*
		 * This is only to please the validation layer, that may or may
		 * not be a bug in the validation layer. That may or may not be
		 * fixed in the future version of the validation layer.
		 */
		vk_set_image_layout(                 //
		    vk,                              // vk_bundle
		    cmd_buffer,                      // cmd_buffer
		    sc->base.images[i],              // image
		    0,                               // src_access_mask
		    barrier_access_mask,             // dst_access_mask
		    VK_IMAGE_LAYOUT_UNDEFINED,       // old_layout
		    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, // new_layout
		    subresource_range);              // subresource_range
	}

	ret = vk_submit_cmd_buffer(vk, cmd_buffer);
	if (ret != VK_SUCCESS) {
		return XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS;
	}

	// Prerecord command buffers for swapchain image ownership/layout
	// transitions
	for (uint32_t i = 0; i < xsc->image_count; i++) {
		ret = vk_init_cmd_buffer(vk, &sc->acquire[i]);
		if (ret != VK_SUCCESS) {
			return XRT_ERROR_VULKAN;
		}
		ret = vk_init_cmd_buffer(vk, &sc->release[i]);
		if (ret != VK_SUCCESS) {
			return XRT_ERROR_VULKAN;
		}

		VkImageSubresourceRange subresource_range = {
		    .aspectMask = barrier_aspect_mask,
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
		    .dstAccessMask = barrier_access_mask,
		    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		    .newLayout = barrier_optimal_layout,
		    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		    .image = sc->base.images[i],
		    .subresourceRange = subresource_range,
		};

		VkImageMemoryBarrier release = {
		    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		    .srcAccessMask = barrier_access_mask,
		    .dstAccessMask = 0,
		    .oldLayout = barrier_optimal_layout,
		    .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		    .srcQueueFamilyIndex = vk->queue_family_index,
		    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL,
		    .image = sc->base.images[i],
		    .subresourceRange = subresource_range,
		};

		//! @todo less conservative pipeline stage masks based on usage
		vk->vkCmdPipelineBarrier(sc->acquire[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &acquire);
		vk->vkCmdPipelineBarrier(sc->release[i], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &release);

		ret = vk->vkEndCommandBuffer(sc->acquire[i]);
		if (ret != VK_SUCCESS) {
			VK_ERROR(vk, "vkEndCommandBuffer: %s", vk_result_string(ret));
			return XRT_ERROR_VULKAN;
		}
		ret = vk->vkEndCommandBuffer(sc->release[i]);
		if (ret != VK_SUCCESS) {
			VK_ERROR(vk, "vkEndCommandBuffer: %s", vk_result_string(ret));
			return XRT_ERROR_VULKAN;
		}

		VkFenceCreateInfo fence_create_info = {
		    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};
		vk->vkCreateFence(vk->device, &fence_create_info, NULL, &sc->acquire_release_fence[i]);
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
	COMP_TRACE_MARKER();

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
	for (uint32_t i = 0; i < xcn->base.info.format_count; i++) {
		c->base.base.info.formats[i] = xcn->base.info.formats[i];
	}

	c->base.base.info.format_count = xcn->base.info.format_count;

	// Default to info.
	enum u_logging_level log_level = U_LOGGING_INFO;

	ret = vk_init_from_given( //
	    &c->vk,               // vk_bundle
	    getProc,              // vkGetInstanceProcAddr
	    instance,             // instance
	    physicalDevice,       // physical_device
	    device,               // device
	    queueFamilyIndex,     // queue_family_index
	    queueIndex,           // queue_index
	    log_level);           // log_level
	if (ret != VK_SUCCESS) {
		goto err_free;
	}

	ret = vk_init_mutex(&c->vk);
	if (ret != VK_SUCCESS) {
		goto err_free;
	}
	return c;

err_free:
	free(c);
	return NULL;
}
