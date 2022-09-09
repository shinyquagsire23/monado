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
#include "util/u_handles.h"
#include "util/u_trace_marker.h"

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
 * Semaphore helpers.
 *
 */

#ifdef VK_KHR_timeline_semaphore
static xrt_result_t
setup_semaphore(struct client_vk_compositor *c)
{
	xrt_graphics_sync_handle_t handle = XRT_GRAPHICS_SYNC_HANDLE_INVALID;
	struct xrt_compositor_semaphore *xcsem = NULL;
	struct vk_bundle *vk = &c->vk;
	xrt_result_t xret;
	VkResult ret;

	xret = xrt_comp_create_semaphore(&c->xcn->base, &handle, &xcsem);
	if (xret != XRT_SUCCESS) {
		U_LOG_E("Failed to create semaphore!");
		return xret;
	}

	VkSemaphore semaphore = VK_NULL_HANDLE;
	ret = vk_create_timeline_semaphore_from_native(vk, handle, &semaphore);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateSemaphore: %s", vk_result_string(ret));
		u_graphics_sync_unref(&handle);
		xrt_compositor_semaphore_reference(&xcsem, NULL);
		return XRT_ERROR_VULKAN;
	}

	c->sync.semaphore = semaphore;
	c->sync.xcsem = xcsem; // No need to reference.

	return XRT_SUCCESS;
}
#endif


/*
 *
 * Frame submit helpers.
 *
 */

static bool
submit_handle(struct client_vk_compositor *c,
              int64_t frame_id,
              xrt_graphics_sync_handle_t sync_handle,
              xrt_result_t *out_xret)
{
	// Did we get a ready made handle, assume it's in the command stream and call commit directly.
	if (!xrt_graphics_sync_handle_is_valid(sync_handle)) {
		return false;
	}

	// Commit consumes the sync_handle.
	*out_xret = xrt_comp_layer_commit(&c->xcn->base, frame_id, sync_handle);
	return true;
}

static bool
submit_semaphore(struct client_vk_compositor *c, int64_t frame_id, xrt_result_t *out_xret)
{
#ifdef VK_KHR_timeline_semaphore
	if (c->sync.xcsem == NULL) {
		return false;
	}

	struct vk_bundle *vk = &c->vk;
	VkResult ret;

	VkSemaphore semaphores[1] = {
	    c->sync.semaphore,
	};
	uint64_t values[1] = {
	    ++(c->sync.value),
	};

	VkTimelineSemaphoreSubmitInfo semaphore_submit_info = {
	    .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
	    .waitSemaphoreValueCount = 0,
	    .pWaitSemaphoreValues = NULL,
	    .signalSemaphoreValueCount = ARRAY_SIZE(values),
	    .pSignalSemaphoreValues = values,
	};
	VkSubmitInfo submit_info = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .pNext = &semaphore_submit_info,
	    .signalSemaphoreCount = ARRAY_SIZE(semaphores),
	    .pSignalSemaphores = semaphores,
	};

	ret = vk->vkQueueSubmit( //
	    vk->queue,           // queue
	    1,                   // submitCount
	    &submit_info,        // pSubmits
	    VK_NULL_HANDLE);     // fence
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkQueueSubmit: %s", vk_result_string(ret));
		*out_xret = XRT_ERROR_VULKAN;
		return true;
	}

	*out_xret = xrt_comp_layer_commit_with_semaphore( //
	    &c->xcn->base,                                // xc
	    frame_id,                                     // frame_id
	    c->sync.xcsem,                                // xcsem
	    values[0]);                                   // value

	return true;
#else
	return false;
#endif
}

static bool
submit_fence(struct client_vk_compositor *c, int64_t frame_id, xrt_result_t *out_xret)
{
	xrt_graphics_sync_handle_t sync_handle = XRT_GRAPHICS_SYNC_HANDLE_INVALID;
	struct vk_bundle *vk = &c->vk;
	VkResult ret;

#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	// Using sync fds currently to match OpenGL extension.
	bool sync_fence = vk->external.fence_sync_fd;
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	bool sync_fence = vk->external.fence_win32_handle;
#else
#error "Need port to export fence sync handles"
#endif

	if (!sync_fence) {
		return false;
	}

	{
		COMP_TRACE_IDENT(create_and_submit_fence);

		ret = vk_create_and_submit_fence_native(vk, &sync_handle);
		if (ret != VK_SUCCESS) {
			// This is really bad, log again.
			U_LOG_E("Could not create and submit a native fence!");
			*out_xret = XRT_ERROR_VULKAN;
			return true; // If we fail we should still return.
		}
	}

	*out_xret = xrt_comp_layer_commit(&c->xcn->base, frame_id, sync_handle);
	return true;
}

static bool
submit_fallback(struct client_vk_compositor *c, int64_t frame_id, xrt_result_t *out_xret)
{
	struct vk_bundle *vk = &c->vk;

	{
		COMP_TRACE_IDENT(device_wait_idle);

		// Last course of action fallback.
		vk->vkDeviceWaitIdle(vk->device);
	}

	*out_xret = xrt_comp_layer_commit(&c->xcn->base, frame_id, XRT_GRAPHICS_SYNC_HANDLE_INVALID);
	return true;
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

	// Make sure images are not used anymore.
	vk->vkDeviceWaitIdle(vk->device);

	for (uint32_t i = 0; i < sc->base.base.image_count; i++) {
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

	uint32_t index = 0;

	// Pipe down call into native swapchain.
	xrt_result_t xret = xrt_swapchain_acquire_image(&sc->xscn->base, &index);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	VkResult ret;


	COMP_TRACE_IDENT(submit);

	// Acquire ownership and complete layout transition
	VkSubmitInfo submitInfo = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &sc->acquire[index],
	};

	// Note we do not submit a fence here, it's not needed.
	ret = vk_locked_submit(vk, vk->queue, 1, &submitInfo, VK_NULL_HANDLE);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "Could not submit to queue: %d", ret);
		return XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS;
	}

	// Finally done.
	*out_index = index;

	return XRT_SUCCESS;
}

static xrt_result_t
client_vk_swapchain_wait_image(struct xrt_swapchain *xsc, uint64_t timeout_ns, uint32_t index)
{
	COMP_TRACE_MARKER();

	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);

	// Pipe down call into native swapchain.
	return xrt_swapchain_wait_image(&sc->xscn->base, timeout_ns, index);
}

static xrt_result_t
client_vk_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	COMP_TRACE_MARKER();

	struct client_vk_swapchain *sc = client_vk_swapchain(xsc);
	struct vk_bundle *vk = &sc->c->vk;

	VkResult ret;

	{
		COMP_TRACE_IDENT(submit);

		// Release ownership and begin layout transition
		VkSubmitInfo submitInfo = {
		    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		    .commandBufferCount = 1,
		    .pCommandBuffers = &sc->release[index],
		};

		// Note we do not submit a fence here, it's not needed.
		ret = vk_locked_submit(vk, vk->queue, 1, &submitInfo, VK_NULL_HANDLE);
		if (ret != VK_SUCCESS) {
			VK_ERROR(vk, "Could not submit to queue: %d", ret);
			return XRT_ERROR_FAILED_TO_SUBMIT_VULKAN_COMMANDS;
		}
	}

	COMP_TRACE_IDENT(release_image);

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

	if (c->sync.semaphore != VK_NULL_HANDLE) {
		vk->vkDestroySemaphore(vk->device, c->sync.semaphore, NULL);
		c->sync.semaphore = VK_NULL_HANDLE;
	}
	xrt_compositor_semaphore_reference(&c->sync.xcsem, NULL);

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
	struct xrt_swapchain *l_xscn;
	struct xrt_swapchain *r_xscn;

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
	struct xrt_swapchain *l_xscn;
	struct xrt_swapchain *r_xscn;
	struct xrt_swapchain *l_d_xscn;
	struct xrt_swapchain *r_d_xscn;

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

	xrt_result_t xret = XRT_SUCCESS;
	if (submit_handle(c, frame_id, sync_handle, &xret)) {
		return xret;
	} else if (submit_semaphore(c, frame_id, &xret)) {
		return xret;
	} else if (submit_fence(c, frame_id, &xret)) {
		return xret;
	} else if (submit_fallback(c, frame_id, &xret)) {
		return xret;
	} else {
		// Really bad state.
		return XRT_ERROR_VULKAN;
	}
}

static xrt_result_t
client_vk_compositor_get_swapchain_create_properties(struct xrt_compositor *xc,
                                                     const struct xrt_swapchain_create_info *info,
                                                     struct xrt_swapchain_create_properties *xsccp)
{
	struct client_vk_compositor *c = client_vk_compositor(xc);

	return xrt_comp_get_swapchain_create_properties(&c->xcn->base, info, xsccp);
}

static xrt_result_t
client_vk_swapchain_create(struct xrt_compositor *xc,
                           const struct xrt_swapchain_create_info *info,
                           struct xrt_swapchain **out_xsc)
{
	COMP_TRACE_MARKER();

	struct client_vk_compositor *c = client_vk_compositor(xc);
	struct vk_bundle *vk = &c->vk;
	VkResult ret;
	xrt_result_t xret;

	struct xrt_swapchain_native *xscn = NULL; // Has to be NULL.
	xret = xrt_comp_native_create_swapchain(c->xcn, info, &xscn);

	if (xret != XRT_SUCCESS) {
		return xret;
	}
	assert(xscn != NULL);

	struct xrt_swapchain *xsc = &xscn->base;

	VkAccessFlags barrier_access_mask = vk_csci_get_barrier_access_mask(info->bits);
	VkImageLayout barrier_optimal_layout = vk_csci_get_barrier_optimal_layout(info->format);
	VkImageAspectFlags barrier_aspect_mask = vk_csci_get_barrier_aspect_mask(info->format);

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
	}

	// Prerecord command buffers for swapchain image ownership/layout transitions
	for (uint32_t i = 0; i < xsc->image_count; i++) {
		ret = vk_cmd_buffer_create_and_begin(vk, &sc->acquire[i]);
		if (ret != VK_SUCCESS) {
			return XRT_ERROR_VULKAN;
		}
		ret = vk_cmd_buffer_create_and_begin(vk, &sc->release[i]);
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
		vk->vkCmdPipelineBarrier(               //
		    sc->acquire[i],                     // commandBuffer
		    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,  // srcStageMask
		    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, // dstStageMask
		    0,                                  // dependencyFlags
		    0,                                  // memoryBarrierCount
		    NULL,                               // pMemoryBarriers
		    0,                                  // bufferMemoryBarrierCount
		    NULL,                               // pBufferMemoryBarriers
		    1,                                  // imageMemoryBarrierCount
		    &acquire);                          // pImageMemoryBarriers

		vk->vkCmdPipelineBarrier(                 //
		    sc->release[i],                       // commandBuffer
		    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,   // srcStageMask
		    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, // dstStageMask
		    0,                                    // dependencyFlags
		    0,                                    // memoryBarrierCount
		    NULL,                                 // pMemoryBarriers
		    0,                                    // bufferMemoryBarrierCount
		    NULL,                                 // pBufferMemoryBarriers
		    1,                                    // imageMemoryBarrierCount
		    &release);                            // pImageMemoryBarriers

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
                            bool external_fence_fd_enabled,
                            bool external_semaphore_fd_enabled,
                            bool timeline_semaphore_enabled,
                            uint32_t queueFamilyIndex,
                            uint32_t queueIndex)
{
	COMP_TRACE_MARKER();

	xrt_result_t xret;
	VkResult ret;
	struct client_vk_compositor *c = U_TYPED_CALLOC(struct client_vk_compositor);

	c->base.base.get_swapchain_create_properties = client_vk_compositor_get_swapchain_create_properties;
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

	ret = vk_init_from_given(          //
	    &c->vk,                        // vk_bundle
	    getProc,                       // vkGetInstanceProcAddr
	    instance,                      // instance
	    physicalDevice,                // physical_device
	    device,                        // device
	    queueFamilyIndex,              // queue_family_index
	    queueIndex,                    // queue_index
	    external_fence_fd_enabled,     // external_fence_fd_enabled
	    external_semaphore_fd_enabled, // external_semaphore_fd_enabled
	    timeline_semaphore_enabled,    // timeline_semaphore_enabled
	    log_level);                    // log_level
	if (ret != VK_SUCCESS) {
		goto err_free;
	}

	ret = vk_init_mutex(&c->vk);
	if (ret != VK_SUCCESS) {
		goto err_free;
	}

#ifdef VK_KHR_timeline_semaphore
	if (vk_can_import_and_export_timeline_semaphore(&c->vk)) {
		xret = setup_semaphore(c);
		if (xret != XRT_SUCCESS) {
			goto err_mutex;
		}
	}
#endif

	return c;


err_mutex:
	vk_deinit_mutex(&c->vk);
err_free:
	free(c);

	return NULL;
}
