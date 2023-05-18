// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor rendering code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup comp_main
 */

#include "render/render_interface.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_compositor.h"

#include "os/os_time.h"

#include "math/m_api.h"
#include "math/m_matrix_2x2.h"
#include "math/m_space.h"

#include "util/u_misc.h"
#include "util/u_trace_marker.h"
#include "util/u_distortion_mesh.h"
#include "util/u_sink.h"
#include "util/u_var.h"
#include "util/u_frame_times_widget.h"

#include "main/comp_layer_renderer.h"
#include "main/comp_frame.h"
#include "main/comp_mirror_to_debug_gui.h"

#ifdef XRT_FEATURE_WINDOW_PEEK
#include "main/comp_window_peek.h"
#endif

#include "vk/vk_helpers.h"
#include "vk/vk_cmd.h"
#include "vk/vk_image_readback_to_xf_pool.h"
#include "vk/vk_cmd.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <inttypes.h>


/*
 *
 * Small internal helpers.
 *
 */

#define CHAIN(STRUCT, NEXT)                                                                                            \
	do {                                                                                                           \
		(STRUCT).pNext = NEXT;                                                                                 \
		NEXT = (VkBaseInStructure *)&(STRUCT);                                                                 \
	} while (false)


/*
 *
 * Private struct(s).
 *
 */

/*!
 * Holds associated vulkan objects and state to render with a distortion.
 *
 * @ingroup comp_main
 */
struct comp_renderer
{
	//! @name Durable members
	//! @brief These don't require the images to be created and don't depend on it.
	//! @{

	//! The compositor we were created by
	struct comp_compositor *c;
	struct comp_settings *settings;

	struct comp_mirror_to_debug_gui mirror_to_debug_gui;

	//! @}

	//! @name Image-dependent members
	//! @{

	//! Index of the current buffer/image
	int32_t acquired_buffer;

	//! Which buffer was last submitted and has a fence pending.
	int32_t fenced_buffer;

	/*!
	 * Array of "rendering" target resources equal in size to the number of
	 * comp_target images. Each target resources holds all of the resources
	 * needed to render to that target and its views.
	 */
	struct render_gfx_target_resources *rtr_array;

	/*!
	 * Array of fences equal in size to the number of comp_target images.
	 */
	VkFence *fences;

	/*!
	 * The number of renderings/fences we've created: set from comp_target when we use that data.
	 */
	uint32_t buffer_count;

	/*!
	 * @brief The layer renderer, which actually knows how to composite layers.
	 *
	 * Depends on the target extents.
	 */
	struct comp_layer_renderer *lr;
	//! @}
};


/*
 *
 * Functions.
 *
 */

static void
renderer_wait_gpu_idle(struct comp_renderer *r)
{
	COMP_TRACE_MARKER();
	struct vk_bundle *vk = &r->c->base.vk;

	os_mutex_lock(&vk->queue_mutex);
	vk->vkDeviceWaitIdle(vk->device);
	os_mutex_unlock(&vk->queue_mutex);
}

static void
calc_viewport_data(struct comp_renderer *r,
                   struct render_viewport_data *out_l_viewport_data,
                   struct render_viewport_data *out_r_viewport_data)
{
	struct comp_compositor *c = r->c;

	bool pre_rotate = false;
	if (r->c->target->surface_transform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
	    r->c->target->surface_transform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
		COMP_SPEW(c, "Swapping width and height, since we are pre rotating");
		pre_rotate = true;
	}

	int w_i32 = pre_rotate ? r->c->xdev->hmd->screens[0].h_pixels : r->c->xdev->hmd->screens[0].w_pixels;
	int h_i32 = pre_rotate ? r->c->xdev->hmd->screens[0].w_pixels : r->c->xdev->hmd->screens[0].h_pixels;

	float scale_x = (float)r->c->target->width / (float)w_i32;
	float scale_y = (float)r->c->target->height / (float)h_i32;

	struct xrt_view *l_v = &r->c->xdev->hmd->views[0];
	struct xrt_view *r_v = &r->c->xdev->hmd->views[1];

	struct render_viewport_data l_viewport_data;
	struct render_viewport_data r_viewport_data;

	if (pre_rotate) {
		l_viewport_data = (struct render_viewport_data){
		    .x = (uint32_t)(l_v->viewport.y_pixels * scale_x),
		    .y = (uint32_t)(l_v->viewport.x_pixels * scale_y),
		    .w = (uint32_t)(l_v->viewport.h_pixels * scale_x),
		    .h = (uint32_t)(l_v->viewport.w_pixels * scale_y),
		};
		r_viewport_data = (struct render_viewport_data){
		    .x = (uint32_t)(r_v->viewport.y_pixels * scale_x),
		    .y = (uint32_t)(r_v->viewport.x_pixels * scale_y),
		    .w = (uint32_t)(r_v->viewport.h_pixels * scale_x),
		    .h = (uint32_t)(r_v->viewport.w_pixels * scale_y),
		};
	} else {
		l_viewport_data = (struct render_viewport_data){
		    .x = (uint32_t)(l_v->viewport.x_pixels * scale_x),
		    .y = (uint32_t)(l_v->viewport.y_pixels * scale_y),
		    .w = (uint32_t)(l_v->viewport.w_pixels * scale_x),
		    .h = (uint32_t)(l_v->viewport.h_pixels * scale_y),
		};
		r_viewport_data = (struct render_viewport_data){
		    .x = (uint32_t)(r_v->viewport.x_pixels * scale_x),
		    .y = (uint32_t)(r_v->viewport.y_pixels * scale_y),
		    .w = (uint32_t)(r_v->viewport.w_pixels * scale_x),
		    .h = (uint32_t)(r_v->viewport.h_pixels * scale_y),
		};
	}

	*out_l_viewport_data = l_viewport_data;
	*out_r_viewport_data = r_viewport_data;
}

//! @pre comp_target_has_images(r->c->target)
static void
renderer_build_rendering_target_resources(struct comp_renderer *r,
                                          struct render_gfx_target_resources *rtr,
                                          uint32_t index)
{
	COMP_TRACE_MARKER();

	struct comp_compositor *c = r->c;

	struct render_gfx_target_data data;
	data.format = r->c->target->format;
	data.is_external = true;
	data.width = r->c->target->width;
	data.height = r->c->target->height;

	render_gfx_target_resources_init(rtr, &c->nr, r->c->target->images[index].view, &data);
}

/*!
 * @pre render_gfx_init(rr, &c->nr)
 * @pre comp_target_has_images(r->c->target)
 */
static void
renderer_build_rendering(struct comp_renderer *r,
                         struct render_gfx *rr,
                         struct render_gfx_target_resources *rtr,
                         VkSampler src_samplers[2],
                         VkImageView src_image_views[2],
                         struct xrt_normalized_rect src_norm_rects[2])
{
	COMP_TRACE_MARKER();

	struct comp_compositor *c = r->c;


	/*
	 * Rendering
	 */

	bool pre_rotate = false;
	if (r->c->target->surface_transform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
	    r->c->target->surface_transform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
		COMP_SPEW(c, "Swapping width and height, since we are pre rotating");
		pre_rotate = true;
	}

	struct render_viewport_data l_viewport_data;
	struct render_viewport_data r_viewport_data;

	calc_viewport_data(r, &l_viewport_data, &r_viewport_data);

	struct xrt_view *l_v = &r->c->xdev->hmd->views[0];
	struct xrt_view *r_v = &r->c->xdev->hmd->views[1];


	/*
	 * Begin
	 */

	render_gfx_begin(rr);


	/*
	 * Update
	 */

	struct render_gfx_mesh_ubo_data distortion_data[2] = {
	    {
	        .vertex_rot = l_v->rot,
	        .post_transform = src_norm_rects[0],
	    },
	    {
	        .vertex_rot = r_v->rot,
	        .post_transform = src_norm_rects[1],
	    },
	};

	const struct xrt_matrix_2x2 rotation_90_cw = {{
	    .vecs =
	        {
	            {0, 1},
	            {-1, 0},
	        },
	}};

	if (pre_rotate) {
		m_mat2x2_multiply(&distortion_data[0].vertex_rot,  //
		                  &rotation_90_cw,                 //
		                  &distortion_data[0].vertex_rot); //
		m_mat2x2_multiply(&distortion_data[1].vertex_rot,  //
		                  &rotation_90_cw,                 //
		                  &distortion_data[1].vertex_rot); //
	}

	render_gfx_update_distortion(rr,                   //
	                             0,                    // view_index
	                             src_samplers[0],      //
	                             src_image_views[0],   //
	                             &distortion_data[0]); //

	render_gfx_update_distortion(rr,                   //
	                             1,                    // view_index
	                             src_samplers[1],      //
	                             src_image_views[1],   //
	                             &distortion_data[1]); //


	/*
	 * Target
	 */

	render_gfx_begin_target( //
	    rr,                  //
	    rtr);                //


	/*
	 * Viewport one
	 */

	render_gfx_begin_view(rr,                //
	                      0,                 // view_index
	                      &l_viewport_data); // viewport_data

	render_gfx_distortion(rr);

	render_gfx_end_view(rr);


	/*
	 * Viewport two
	 */

	render_gfx_begin_view(rr,                //
	                      1,                 // view_index
	                      &r_viewport_data); // viewport_data

	render_gfx_distortion(rr);

	render_gfx_end_view(rr);


	/*
	 * End
	 */

	render_gfx_end_target(rr);

	// Make the command buffer usable.
	render_gfx_end(rr);
}

/*!
 * @pre comp_target_has_images(r->c->target)
 * Update r->buffer_count before calling.
 */
static void
renderer_create_renderings_and_fences(struct comp_renderer *r)
{
	assert(r->fences == NULL);
	if (r->buffer_count == 0) {
		COMP_ERROR(r->c, "Requested 0 command buffers.");
		return;
	}

	COMP_DEBUG(r->c, "Allocating %d Command Buffers.", r->buffer_count);

	struct vk_bundle *vk = &r->c->base.vk;

	bool use_compute = r->settings->use_compute;
	if (!use_compute) {
		r->rtr_array = U_TYPED_ARRAY_CALLOC(struct render_gfx_target_resources, r->buffer_count);

		for (uint32_t i = 0; i < r->buffer_count; ++i) {
			renderer_build_rendering_target_resources(r, &r->rtr_array[i], i);
		}
	}

	r->fences = U_TYPED_ARRAY_CALLOC(VkFence, r->buffer_count);

	for (uint32_t i = 0; i < r->buffer_count; i++) {
		VkFenceCreateInfo fence_info = {
		    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};

		VkResult ret = vk->vkCreateFence( //
		    vk->device,                   //
		    &fence_info,                  //
		    NULL,                         //
		    &r->fences[i]);               //
		if (ret != VK_SUCCESS) {
			COMP_ERROR(r->c, "vkCreateFence: %s", vk_result_string(ret));
		}
	}
}

static void
renderer_close_renderings_and_fences(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->base.vk;
	// Renderings
	if (r->buffer_count > 0 && r->rtr_array != NULL) {
		for (uint32_t i = 0; i < r->buffer_count; i++) {
			render_gfx_target_resources_close(&r->rtr_array[i]);
		}

		free(r->rtr_array);
		r->rtr_array = NULL;
	}

	// Fences
	if (r->buffer_count > 0 && r->fences != NULL) {
		for (uint32_t i = 0; i < r->buffer_count; i++) {
			vk->vkDestroyFence(vk->device, r->fences[i], NULL);
			r->fences[i] = VK_NULL_HANDLE;
		}
		free(r->fences);
		r->fences = NULL;
	}

	r->buffer_count = 0;
	r->acquired_buffer = -1;
	r->fenced_buffer = -1;
}

//! @pre comp_target_check_ready(r->c->target)
static void
renderer_create_layer_renderer(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->base.vk;

	assert(comp_target_check_ready(r->c->target));

	uint32_t layer_count = 0;
	if (r->lr != NULL) {
		// if we already had one, re-populate it after recreation.
		layer_count = r->lr->layer_count;
		comp_layer_renderer_destroy(&r->lr);
	}

	VkExtent2D extent;

	extent = (VkExtent2D){
	    .width = r->c->view_extents.width,
	    .height = r->c->view_extents.height,
	};

	r->lr = comp_layer_renderer_create(vk, &r->c->shaders, extent, VK_FORMAT_B8G8R8A8_SRGB);
	if (layer_count != 0) {
		comp_layer_renderer_allocate_layers(r->lr, layer_count);
	}
}

/*!
 * @brief Ensure that target images and renderings are created, if possible.
 *
 * @param r Self pointer
 * @param force_recreate If true, will tear down and re-create images and renderings, e.g. for a resize
 *
 * @returns true if images and renderings are ready and created.
 *
 * @private @memberof comp_renderer
 * @ingroup comp_main
 */
static bool
renderer_ensure_images_and_renderings(struct comp_renderer *r, bool force_recreate)
{
	struct comp_compositor *c = r->c;
	struct comp_target *target = c->target;

	if (!comp_target_check_ready(target)) {
		// Not ready, so can't render anything.
		return false;
	}

	// We will create images if we don't have any images or if we were told to recreate them.
	bool create = force_recreate || !comp_target_has_images(target) || (r->buffer_count == 0);
	if (!create) {
		return true;
	}

	COMP_DEBUG(c, "Creating images and renderings (force_recreate: %s).", force_recreate ? "true" : "false");

	/*
	 * This makes sure that any pending command buffer has completed
	 * and all resources referred by it can now be manipulated. This
	 * make sure that validation doesn't complain. This is done
	 * during resize so isn't time critical.
	 */
	renderer_wait_gpu_idle(r);

	// Make we sure we destroy all dependent things before creating new images.
	renderer_close_renderings_and_fences(r);

	VkImageUsageFlags image_usage = 0;
	if (r->settings->use_compute) {
		image_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
	} else {
		image_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}

	if (c->peek) {
		image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	comp_target_create_images(           //
	    r->c->target,                    //
	    r->c->settings.preferred.width,  //
	    r->c->settings.preferred.height, //
	    r->settings->color_format,       //
	    r->settings->color_space,        //
	    image_usage,                     //
	    r->settings->present_mode);      //

	bool pre_rotate = false;
	if (r->c->target->surface_transform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
	    r->c->target->surface_transform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
		pre_rotate = true;
	}

	// @todo: is it safe to fail here?
	if (!render_distortion_images_ensure(&r->c->nr, &r->c->base.vk, r->c->xdev, pre_rotate))
		return false;

	r->buffer_count = r->c->target->image_count;

	renderer_create_layer_renderer(r);
	renderer_create_renderings_and_fences(r);

	assert(r->buffer_count != 0);

	return true;
}

//! Create renderer and initialize non-image-dependent members
static void
renderer_init(struct comp_renderer *r, struct comp_compositor *c)
{
	r->c = c;
	r->settings = &c->settings;

	r->acquired_buffer = -1;
	r->fenced_buffer = -1;
	r->rtr_array = NULL;

	// Try to early-allocate these, in case we can.
	renderer_ensure_images_and_renderings(r, false);

	struct vk_bundle *vk = &r->c->base.vk;

	VkResult ret = comp_mirror_init(&r->mirror_to_debug_gui, vk, &c->shaders, r->lr->extent);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "comp_mirror_init: %s", vk_result_string(ret));
		assert(false && "Whelp, can't return a error. But should never really fail.");
	}
}

static void
renderer_wait_for_last_fence(struct comp_renderer *r)
{
	COMP_TRACE_MARKER();

	if (r->fenced_buffer < 0) {
		return;
	}

	struct vk_bundle *vk = &r->c->base.vk;
	VkResult ret;

	ret = vk->vkWaitForFences(vk->device, 1, &r->fences[r->fenced_buffer], VK_TRUE, UINT64_MAX);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkWaitForFences: %s", vk_result_string(ret));
	}

	r->fenced_buffer = -1;
}

static void
renderer_submit_queue(struct comp_renderer *r, VkCommandBuffer cmd, VkPipelineStageFlags pipeline_stage_flag)
{
	COMP_TRACE_MARKER();

	struct vk_bundle *vk = &r->c->base.vk;
	VkResult ret;


	/*
	 * Wait for previous frame's work to complete.
	 */

	// Wait for the last fence, if any.
	renderer_wait_for_last_fence(r);
	assert(r->fenced_buffer < 0);

	assert(r->acquired_buffer >= 0);
	ret = vk->vkResetFences(vk->device, 1, &r->fences[r->acquired_buffer]);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkResetFences: %s", vk_result_string(ret));
	}


	/*
	 * Regular semaphore setup.
	 */

	struct comp_target *ct = r->c->target;
#define WAIT_SEMAPHORE_COUNT 1

	VkSemaphore wait_sems[WAIT_SEMAPHORE_COUNT] = {ct->semaphores.present_complete};
	VkPipelineStageFlags stage_flags[WAIT_SEMAPHORE_COUNT] = {pipeline_stage_flag};

	VkSemaphore *wait_sems_ptr = NULL;
	VkPipelineStageFlags *stage_flags_ptr = NULL;
	uint32_t wait_sem_count = 0;
	if (wait_sems[0] != VK_NULL_HANDLE) {
		wait_sems_ptr = wait_sems;
		stage_flags_ptr = stage_flags;
		wait_sem_count = WAIT_SEMAPHORE_COUNT;
	}

	// Next pointer for VkSubmitInfo
	const void *next = NULL;

#ifdef VK_KHR_timeline_semaphore
	assert(!comp_frame_is_invalid_locked(&r->c->frame.rendering));
	uint64_t render_complete_signal_values[WAIT_SEMAPHORE_COUNT] = {(uint64_t)r->c->frame.rendering.id};

	VkTimelineSemaphoreSubmitInfoKHR timeline_info = {
	    .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
	};

	if (ct->semaphores.render_complete_is_timeline) {
		timeline_info = (VkTimelineSemaphoreSubmitInfoKHR){
		    .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
		    .signalSemaphoreValueCount = WAIT_SEMAPHORE_COUNT,
		    .pSignalSemaphoreValues = render_complete_signal_values,
		};

		CHAIN(timeline_info, next);
	}
#endif


	VkSubmitInfo comp_submit_info = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .pNext = next,
	    .pWaitDstStageMask = stage_flags_ptr,
	    .pWaitSemaphores = wait_sems_ptr,
	    .waitSemaphoreCount = wait_sem_count,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &cmd,
	    .signalSemaphoreCount = 1,
	    .pSignalSemaphores = &ct->semaphores.render_complete,
	};

	/*
	 * The renderer command buffer pool is only accessed from one thread,
	 * this satisfies the `_locked` requirement of the function. This lets
	 * us avoid taking a lot of locks. The queue lock will be taken by
	 * @ref vk_cmd_submit_locked tho.
	 */
	ret = vk_cmd_submit_locked(vk, 1, &comp_submit_info, r->fences[r->acquired_buffer]);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkQueueSubmit: %s", vk_result_string(ret));
	}

	// This buffer now have a pending fence.
	r->fenced_buffer = r->acquired_buffer;
}

static void
renderer_get_view_projection(struct comp_renderer *r)
{
	COMP_TRACE_MARKER();

	struct xrt_vec3 default_eye_relation = {
	    0.063000f, /*! @todo get actual ipd_meters */
	    0.0f,
	    0.0f,
	};

	struct xrt_space_relation head_relation = XRT_SPACE_RELATION_ZERO;
	struct xrt_pose poses[2] = {0};

	xrt_device_get_view_poses(                           //
	    r->c->xdev,                                      //
	    &default_eye_relation,                           //
	    r->c->frame.rendering.predicted_display_time_ns, //
	    2,                                               //
	    &head_relation,                                  //
	    r->c->base.slot.fovs,                            //
	    poses);                                          //

	struct xrt_pose base_space_pose = XRT_POSE_IDENTITY;

	for (uint32_t i = 0; i < 2; i++) {
		const struct xrt_fov fov = r->c->base.slot.fovs[i];
		const struct xrt_pose eye_pose = poses[i];

		comp_layer_renderer_set_fov(r->lr, &fov, i);

		struct xrt_space_relation result = {0};
		struct xrt_relation_chain xrc = {0};
		m_relation_chain_push_pose_if_not_identity(&xrc, &eye_pose);
		m_relation_chain_push_relation(&xrc, &head_relation);
		m_relation_chain_push_pose_if_not_identity(&xrc, &base_space_pose);
		m_relation_chain_resolve(&xrc, &result);

		r->c->base.slot.poses[i] = result.pose;
		comp_layer_renderer_set_pose(r->lr, &eye_pose, &result.pose, i);
	}
}

static void
renderer_acquire_swapchain_image(struct comp_renderer *r)
{
	COMP_TRACE_MARKER();

	uint32_t buffer_index = 0;
	VkResult ret;

	assert(r->acquired_buffer < 0);

	if (!renderer_ensure_images_and_renderings(r, false)) {
		// Not ready yet.
		return;
	}
	ret = comp_target_acquire(r->c->target, &buffer_index);

	if ((ret == VK_ERROR_OUT_OF_DATE_KHR) || (ret == VK_SUBOPTIMAL_KHR)) {
		COMP_DEBUG(r->c, "Received %s.", vk_result_string(ret));

		if (!renderer_ensure_images_and_renderings(r, true)) {
			// Failed on force recreate.
			COMP_ERROR(r->c,
			           "renderer_acquire_swapchain_image: comp_target_acquire was out of date, force "
			           "re-create image and renderings failed. Probably the target disappeared.");
			return;
		}

		/* Acquire image again to silence validation error */
		ret = comp_target_acquire(r->c->target, &buffer_index);
		if (ret != VK_SUCCESS) {
			COMP_ERROR(r->c, "comp_target_acquire: %s", vk_result_string(ret));
		}
	} else if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "comp_target_acquire: %s", vk_result_string(ret));
	}

	r->acquired_buffer = buffer_index;
}

static void
renderer_resize(struct comp_renderer *r)
{
	if (!comp_target_check_ready(r->c->target)) {
		// Can't create images right now.
		// Just close any existing renderings.
		renderer_close_renderings_and_fences(r);
		return;
	}
	// Force recreate.
	renderer_ensure_images_and_renderings(r, true);
}

static void
renderer_present_swapchain_image(struct comp_renderer *r, uint64_t desired_present_time_ns, uint64_t present_slop_ns)
{
	COMP_TRACE_MARKER();

	VkResult ret;

	assert(!comp_frame_is_invalid_locked(&r->c->frame.rendering));
	uint64_t render_complete_signal_value = (uint64_t)r->c->frame.rendering.id;

	ret = comp_target_present(        //
	    r->c->target,                 //
	    r->c->base.vk.queue,          //
	    r->acquired_buffer,           //
	    render_complete_signal_value, //
	    desired_present_time_ns,      //
	    present_slop_ns);             //
	r->acquired_buffer = -1;

	if (ret == VK_ERROR_OUT_OF_DATE_KHR || ret == VK_SUBOPTIMAL_KHR) {
		renderer_resize(r);
		return;
	}
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vk_swapchain_present: %s", vk_result_string(ret));
	}
}

static void
renderer_fini(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->base.vk;

	// Command buffers
	renderer_close_renderings_and_fences(r);

	// Do before layer render just in case it holds any references.
	comp_mirror_fini(&r->mirror_to_debug_gui, vk);

	// Do this after the mirror struct.
	comp_layer_renderer_destroy(&(r->lr));
}

static VkImageView
get_image_view(const struct comp_swapchain_image *image, enum xrt_layer_composition_flags flags, uint32_t array_index)
{
	if (flags & XRT_LAYER_COMPOSITION_BLEND_TEXTURE_SOURCE_ALPHA_BIT) {
		return image->views.alpha[array_index];
	}

	return image->views.no_alpha[array_index];
}

/*!
 * @pre render_gfx_init(rr, &c->nr)
 */
static void
do_gfx_mesh_and_proj(struct comp_renderer *r,
                     struct render_gfx *rr,
                     struct render_gfx_target_resources *rts,
                     const struct comp_layer *layer,
                     const struct xrt_layer_projection_view_data *lvd,
                     const struct xrt_layer_projection_view_data *rvd)
{
	const struct xrt_layer_data *data = &layer->data;
	const uint32_t left_array_index = lvd->sub.array_index;
	const uint32_t right_array_index = rvd->sub.array_index;
	const struct comp_swapchain_image *left = &layer->sc_array[0]->images[lvd->sub.image_index];
	const struct comp_swapchain_image *right = &layer->sc_array[1]->images[rvd->sub.image_index];

	struct xrt_normalized_rect src_norm_rects[2] = {lvd->sub.norm_rect, rvd->sub.norm_rect};
	if (data->flip_y) {
		src_norm_rects[0].h = -src_norm_rects[0].h;
		src_norm_rects[0].y = 1 + src_norm_rects[0].y;
		src_norm_rects[1].h = -src_norm_rects[1].h;
		src_norm_rects[1].y = 1 + src_norm_rects[1].y;
	}

	VkSampler clamp_to_border_black = rr->r->samplers.clamp_to_border_black;
	VkSampler src_samplers[2] = {
	    clamp_to_border_black,
	    clamp_to_border_black,
	};

	VkImageView src_image_views[2] = {
	    get_image_view(left, data->flags, left_array_index),
	    get_image_view(right, data->flags, right_array_index),
	};

	renderer_build_rendering(r, rr, rts, src_samplers, src_image_views, src_norm_rects);
}

/*!
 * @pre render_gfx_init(rr, &c->nr)
 */
static void
dispatch_graphics(struct comp_renderer *r, struct render_gfx *rr)
{
	COMP_TRACE_MARKER();

	struct comp_compositor *c = r->c;
	struct comp_target *ct = c->target;

	struct render_gfx_target_resources *rtr = &r->rtr_array[r->acquired_buffer];
	bool one_projection_layer_fast_path = c->base.slot.one_projection_layer_fast_path;

	// No fast path, standard layer renderer path.
	if (!one_projection_layer_fast_path) {
		// We mark here to include the layer rendering in the GPU time.
		comp_target_mark_submit(ct, c->frame.rendering.id, os_monotonic_get_ns());

		renderer_get_view_projection(r);
		comp_layer_renderer_draw(r->lr);

		VkSampler clamp_to_border_black = r->c->nr.samplers.clamp_to_border_black;
		VkSampler src_samplers[2] = {
		    clamp_to_border_black,
		    clamp_to_border_black,
		};
		VkImageView src_image_views[2] = {
		    r->lr->framebuffers[0].view,
		    r->lr->framebuffers[1].view,
		};

		struct xrt_normalized_rect src_norm_rects[2] = {
		    {.x = 0, .y = 0, .w = 1, .h = 1},
		    {.x = 0, .y = 0, .w = 1, .h = 1},
		};

		renderer_build_rendering(r, rr, rtr, src_samplers, src_image_views, src_norm_rects);

		renderer_submit_queue(r, rr->r->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		return;
	}


	/*
	 * Fast path.
	 */

	XRT_MAYBE_UNUSED const uint32_t layer_count = c->base.slot.layer_count;
	assert(layer_count >= 1);

	int i = 0;
	const struct comp_layer *layer = &c->base.slot.layers[i];

	switch (layer->data.type) {
	case XRT_LAYER_STEREO_PROJECTION: {
		const struct xrt_layer_stereo_projection_data *stereo = &layer->data.stereo;
		const struct xrt_layer_projection_view_data *lvd = &stereo->l;
		const struct xrt_layer_projection_view_data *rvd = &stereo->r;

		c->base.slot.poses[0] = lvd->pose;
		c->base.slot.poses[1] = rvd->pose;
		c->base.slot.fovs[0] = lvd->fov;
		c->base.slot.fovs[1] = rvd->fov;

		do_gfx_mesh_and_proj(r, rr, rtr, layer, lvd, rvd);

		renderer_submit_queue(r, rr->r->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		// We mark afterwards to not include CPU time spent.
		comp_target_mark_submit(ct, c->frame.rendering.id, os_monotonic_get_ns());
	} break;

	case XRT_LAYER_STEREO_PROJECTION_DEPTH: {
		const struct xrt_layer_stereo_projection_depth_data *stereo = &layer->data.stereo_depth;
		const struct xrt_layer_projection_view_data *lvd = &stereo->l;
		const struct xrt_layer_projection_view_data *rvd = &stereo->r;

		c->base.slot.poses[0] = lvd->pose;
		c->base.slot.poses[1] = rvd->pose;
		c->base.slot.fovs[0] = lvd->fov;
		c->base.slot.fovs[1] = rvd->fov;

		do_gfx_mesh_and_proj(r, rr, rtr, layer, lvd, rvd);

		renderer_submit_queue(r, rr->r->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		// We mark afterwards to not include CPU time spent.
		comp_target_mark_submit(ct, c->frame.rendering.id, os_monotonic_get_ns());
	} break;

	default: COMP_ERROR(c, "Unhandled case: '%u'", layer->data.type); assert(false);
	}
}


/*
 *
 * Compute
 *
 */

static void
get_view_poses(struct comp_renderer *r, struct xrt_pose out_world[2], struct xrt_pose out_eye[2])
{
	COMP_TRACE_MARKER();

	struct xrt_vec3 default_eye_relation = {
	    0.063000f, /*! @todo get actual ipd_meters */
	    0.0f,
	    0.0f,
	};

	struct xrt_space_relation head_relation = XRT_SPACE_RELATION_ZERO;
	struct xrt_pose poses[2] = {0};

	xrt_device_get_view_poses(                           //
	    r->c->xdev,                                      //
	    &default_eye_relation,                           //
	    r->c->frame.rendering.predicted_display_time_ns, //
	    2,                                               //
	    &head_relation,                                  //
	    r->c->base.slot.fovs,                            //
	    poses);                                          //

	for (uint32_t i = 0; i < 2; i++) {
		const struct xrt_fov fov = r->c->base.slot.fovs[i];
		const struct xrt_pose eye_pose = poses[i];

		comp_layer_renderer_set_fov(r->lr, &fov, i);

		struct xrt_space_relation result = {0};
		struct xrt_relation_chain xrc = {0};
		m_relation_chain_push_pose_if_not_identity(&xrc, &eye_pose);
		m_relation_chain_push_relation(&xrc, &head_relation);
		m_relation_chain_resolve(&xrc, &result);

		out_eye[i] = eye_pose;
		out_world[i] = result.pose;
		r->c->base.slot.poses[i] = result.pose;
	}
}

static void
ensure_scratch_image(struct comp_renderer *r,
                     struct render_viewport_data *out_l_viewport_data,
                     struct render_viewport_data *out_r_viewport_data)
{
	struct xrt_view *l_v = &r->c->xdev->hmd->views[0];
	struct xrt_view *r_v = &r->c->xdev->hmd->views[1];

	uint32_t w = MAX(l_v->viewport.w_pixels, r_v->viewport.w_pixels);
	uint32_t h = MAX(l_v->viewport.h_pixels, r_v->viewport.h_pixels);

	// Adjust size to be bigger, 140%, to match default recommended viewport size.
	//! @todo Make this match fully, or even match app provided layers.
	w = (uint32_t)(w * 1.4f);
	h = (uint32_t)(h * 1.4f);

	struct render_viewport_data l_viewport_data = {
	    .w = w,
	    .h = h,
	    .x = 0,
	    .y = 0,
	};

	struct render_viewport_data r_viewport_data = {
	    .w = w,
	    .h = h,
	    .x = w,
	    .y = 0,
	};

	VkExtent2D extent = {w * 2, h};

	if (!render_ensure_scratch_image(&r->c->nr, extent)) {
		U_LOG_E("Failed to create scratch image!");
		assert(false);
	}

	*out_l_viewport_data = l_viewport_data;
	*out_r_viewport_data = r_viewport_data;
}

static void
do_layers(struct comp_renderer *r,
          struct render_compute *crc,
          const struct comp_layer *layers,
          const uint32_t layer_count)
{
	struct render_viewport_data views[2];

	// Create scratch image and get target views.
	ensure_scratch_image(r, &views[0], &views[1]);

	struct render_compute_layer_ubo_data *ubo_data =
	    (struct render_compute_layer_ubo_data *)crc->r->compute.layer.ubo.mapped;

	for (uint32_t i = 0; i < 2; i++) {
		ubo_data->views[i] = views[i];
	}

	ubo_data->pre_transforms[0] = crc->r->distortion.uv_to_tanangle[0];
	ubo_data->pre_transforms[1] = crc->r->distortion.uv_to_tanangle[1];

	VkSampler clamp_to_edge = crc->r->samplers.clamp_to_edge;
	VkSampler clamp_to_border_black = crc->r->samplers.clamp_to_border_black;

	VkImage target_image = crc->r->scratch.color.image;
	VkImageView target_image_view = crc->r->scratch.color.unorm_view; // Have to write in linear

	struct xrt_pose world_poses[2], eye_poses[2];
	get_view_poses(r, world_poses, eye_poses);

	// Not the transform of the views, but the inverse: actual view matrices.
	struct xrt_matrix_4x4 world_view_mats[2], eye_view_mats[2];
	math_matrix_4x4_view_from_pose(&world_poses[0], &world_view_mats[0]);
	math_matrix_4x4_view_from_pose(&world_poses[1], &world_view_mats[1]);
	math_matrix_4x4_view_from_pose(&eye_poses[0], &eye_view_mats[0]);
	math_matrix_4x4_view_from_pose(&eye_poses[1], &eye_view_mats[1]);

	// Tightly pack color and optional depth images.
	uint32_t cur_image = 0;
	VkSampler src_samplers[COMP_MAX_IMAGES];
	VkImageView src_image_views[COMP_MAX_IMAGES];

	for (uint32_t layer_i = 0; layer_i < layer_count; layer_i++) {
		const struct xrt_layer_data *data = &layers[layer_i].data;
		const struct comp_layer *layer = &layers[layer_i];

		ubo_data->layer_type[layer_i].val = data->type;
		ubo_data->layer_type[layer_i].unpremultiplied =
		    (data->flags & XRT_LAYER_COMPOSITION_UNPREMULTIPLIED_ALPHA_BIT) != 0;

		// Base index into arrays that have a value per view & per layer.
		uint32_t view_index_for_layer = layer_i * COMP_VIEWS_PER_LAYER;

		//! Stop compositing layers if device's sampled image limit is reached.
		//! This is necessary until composition can be split in multiple passes.
		//! @todo: remove this after multi-pass composition is implemented.
		uint32_t required_image_samplers;
		switch (data->type) {
		case XRT_LAYER_STEREO_PROJECTION: required_image_samplers = 2; break;
		case XRT_LAYER_STEREO_PROJECTION_DEPTH: required_image_samplers = 4; break;
		case XRT_LAYER_QUAD: required_image_samplers = 1; break;
		default: required_image_samplers = 0;
		}
		//! Exit loop if shader cannot receive more image samplers
		if (cur_image + required_image_samplers >
		    crc->r->vk->features.max_per_stage_descriptor_sampled_images) {
			for (uint32_t i = layer_i; i < layer_count; i++) {
				ubo_data->layer_type[i].val = UINT32_MAX; //! @todo make this not needed.
			}
			break;
		}


		switch (data->type) {
		case XRT_LAYER_STEREO_PROJECTION_DEPTH:
		case XRT_LAYER_STEREO_PROJECTION: {
			const struct xrt_layer_projection_view_data *lvd = NULL;
			const struct xrt_layer_projection_view_data *rvd = NULL;
			const struct xrt_layer_depth_data *l_dvd = NULL;
			const struct xrt_layer_depth_data *r_dvd = NULL;

			if (data->type == XRT_LAYER_STEREO_PROJECTION) {
				const struct xrt_layer_stereo_projection_data *stereo = &layer->data.stereo;
				lvd = &stereo->l;
				rvd = &stereo->r;
			} else {
				const struct xrt_layer_stereo_projection_depth_data *stereo = &layer->data.stereo_depth;
				lvd = &stereo->l;
				rvd = &stereo->r;
				l_dvd = &stereo->l_d;
				r_dvd = &stereo->r_d;
			}

			uint32_t left_array_index = lvd->sub.array_index;
			uint32_t right_array_index = rvd->sub.array_index;
			const struct comp_swapchain_image *left = &layer->sc_array[0]->images[lvd->sub.image_index];
			const struct comp_swapchain_image *right = &layer->sc_array[1]->images[rvd->sub.image_index];

			// Left
			src_samplers[cur_image] = clamp_to_border_black;
			src_image_views[cur_image] = get_image_view(left, data->flags, left_array_index);
			ubo_data->images_samplers[view_index_for_layer + 0].images[0] = cur_image++;

			// Right
			src_samplers[cur_image] = clamp_to_border_black;
			src_image_views[cur_image] = get_image_view(right, data->flags, right_array_index);
			ubo_data->images_samplers[view_index_for_layer + 1].images[0] = cur_image++;

			// Depth
			if (data->type == XRT_LAYER_STEREO_PROJECTION_DEPTH) {
				uint32_t d_left_array_index = lvd->sub.array_index;
				uint32_t d_right_array_index = rvd->sub.array_index;
				const struct comp_swapchain_image *d_left =
				    &layer->sc_array[2]->images[l_dvd->sub.image_index];
				const struct comp_swapchain_image *d_right =
				    &layer->sc_array[3]->images[r_dvd->sub.image_index];


				// Depth left
				src_samplers[cur_image] = clamp_to_edge; // Edge to keep depth stable at edges.
				src_image_views[cur_image] = get_image_view(d_left, data->flags, d_left_array_index);
				ubo_data->images_samplers[view_index_for_layer + 0].images[1] = cur_image++;

				// Depth right
				src_samplers[cur_image] = clamp_to_edge; // Edge to keep depth stable at edges.
				src_image_views[cur_image] = get_image_view(d_right, data->flags, d_right_array_index);
				ubo_data->images_samplers[view_index_for_layer + 1].images[1] = cur_image++;
			}

			struct xrt_normalized_rect *post_transforms = &ubo_data->post_transforms[view_index_for_layer];
			post_transforms[0] = lvd->sub.norm_rect;
			post_transforms[1] = rvd->sub.norm_rect;
			if (data->flip_y) {
				post_transforms[0].h = -post_transforms[0].h;
				post_transforms[0].y = 1.0f + post_transforms[0].y;
				post_transforms[1].h = -post_transforms[1].h;
				post_transforms[1].y = 1.0f + post_transforms[1].y;
			}

			// unused if timewarp is off
			if (!r->c->debug.atw_off) {
				render_calc_time_warp_matrix(                         //
				    &lvd->pose,                                       //
				    &lvd->fov,                                        //
				    &world_poses[0],                                  //
				    &ubo_data->transforms[view_index_for_layer + 0]); //
				render_calc_time_warp_matrix(                         //
				    &rvd->pose,                                       //
				    &rvd->fov,                                        //
				    &world_poses[1],                                  //
				    &ubo_data->transforms[view_index_for_layer + 1]); //
			}

		} break;
		case XRT_LAYER_QUAD: {
			const struct xrt_layer_quad_data *q = &layer->data.quad;
			const struct comp_swapchain_image *image = &layer->sc_array[0]->images[q->sub.image_index];
			uint32_t array_index = q->sub.array_index;

			// Same image for both views
			src_samplers[cur_image] = clamp_to_edge;
			src_image_views[cur_image] = get_image_view(image, layer->data.flags, array_index);
			ubo_data->images_samplers[view_index_for_layer + 0].images[0] = cur_image;
			ubo_data->images_samplers[view_index_for_layer + 1].images[0] = cur_image;
			cur_image++;


			struct xrt_normalized_rect *post_transforms = &ubo_data->post_transforms[view_index_for_layer];

			// Same image for both views
			post_transforms[0] = q->sub.norm_rect;
			post_transforms[1] = q->sub.norm_rect;

			// quad layers calculated in flipped space than projection layers.
			// Note: different y flip logic compared to projection layers.
			if (!data->flip_y) {
				post_transforms[0].h = -post_transforms[0].h;
				post_transforms[0].y = post_transforms[0].y - post_transforms[0].h;
				post_transforms[1].h = -post_transforms[1].h;
				post_transforms[1].y = post_transforms[1].y - post_transforms[1].h;
			}

			ubo_data->quad_extent[layer_i].val = data->quad.size;

			// Is this layer viewspace or not.
			const struct xrt_matrix_4x4 *view_mats =
			    (layer->data.flags & XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT) ? eye_view_mats
			                                                               : world_view_mats;

			for (uint32_t view_i = 0; view_i < 2; view_i++) {
				// transform quad pose into view space for each view
				math_matrix_4x4_transform_vec3(
				    &view_mats[view_i], &data->quad.pose.position,
				    &ubo_data->quad_position[view_index_for_layer + view_i].val);

				// neutral quad layer faces +z, towards the user
				struct xrt_vec3 normal = (struct xrt_vec3){.x = 0, .y = 0, .z = 1};

				// rotation of the quad normal in world space
				struct xrt_quat rotation = data->quad.pose.orientation;
				math_quat_rotate_vec3(&rotation, &normal, &normal);

				/*
				 * normal is a vector that originates on the plane, not on the origin.
				 * Instead of using the inverse quad transform to transform it into view space we can
				 * simply add up vectors:
				 *
				 * combined_normal [in world space] = plane_origin [in world space] + normal [in plane
				 * space] [with plane in world space]
				 *
				 * Then combined_normal can be transformed to view space via view matrix and a new
				 * normal_view_space retrieved:
				 *
				 * normal_view_space = combined_normal [in view space] - plane_origin [in view space]
				 */
				struct xrt_vec3 normal_view_space = normal;
				math_vec3_accum(&data->quad.pose.position, &normal_view_space);
				math_matrix_4x4_transform_vec3(&view_mats[view_i], &normal_view_space,
				                               &normal_view_space);
				math_vec3_subtract(&ubo_data->quad_position[view_index_for_layer + view_i].val,
				                   &normal_view_space);
				ubo_data->quad_normal[view_index_for_layer + view_i].val = normal_view_space;


				struct xrt_vec3 scale = {1.f, 1.f, 1.f};
				struct xrt_matrix_4x4 plane_transform_view_space;
				math_matrix_4x4_model(&data->quad.pose, &scale, &plane_transform_view_space);
				math_matrix_4x4_multiply(&view_mats[view_i], &plane_transform_view_space,
				                         &plane_transform_view_space);
				math_matrix_4x4_inverse(
				    &plane_transform_view_space,
				    &ubo_data->inverse_quad_transform[view_index_for_layer + view_i]);
			}

			// hide a quad layer by pointing its normal away from the camera in view space
			struct xrt_vec3 hidden_normal = {.x = 0, .y = 0, .z = -1};
			switch (q->visibility) {
			case XRT_LAYER_EYE_VISIBILITY_NONE:
				ubo_data->quad_normal[view_index_for_layer + 0].val = hidden_normal;
				ubo_data->quad_normal[view_index_for_layer + 1].val = hidden_normal;
				break;
			case XRT_LAYER_EYE_VISIBILITY_LEFT_BIT:
				ubo_data->quad_normal[view_index_for_layer + 1].val = hidden_normal;
				break;
			case XRT_LAYER_EYE_VISIBILITY_RIGHT_BIT:
				ubo_data->quad_normal[view_index_for_layer + 0].val = hidden_normal;
				break;
			case XRT_LAYER_EYE_VISIBILITY_BOTH: break;
			}

		} break;
		default:
			COMP_ERROR(r->c, "Layer type %d not supported by compute shader, skipping", data->type);
			ubo_data->layer_type[layer_i].val = UINT32_MAX;
		}
	}

	for (uint32_t i = layer_count; i < COMP_MAX_LAYERS; i++) {
		ubo_data->layer_type[i].val = UINT32_MAX;
	}

	//! @todo: If Vulkan 1.2, use VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT and skip this
	while (cur_image < crc->r->compute.layer.image_array_size) {
		src_samplers[cur_image] = clamp_to_edge;
		src_image_views[cur_image] = crc->r->mock.color.image_view;
		cur_image++;
	}

	render_compute_layers(                        //
	    crc,                                      //
	    src_samplers,                             //
	    src_image_views,                          //
	    cur_image,                                //
	    target_image,                             //
	    target_image_view,                        //
	    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, //
	    !r->c->debug.atw_off);                    //
}

static void
do_distortion(struct comp_renderer *r, struct render_compute *crc, const struct render_viewport_data views[2])
{
	VkImage target_image = r->c->target->images[r->acquired_buffer].handle;
	VkImageView target_image_view = r->c->target->images[r->acquired_buffer].view;

	VkImageView view = crc->r->scratch.color.srgb_view; // Read with gamma curve.
	VkSampler sampler = crc->r->samplers.clamp_to_border_black;

	VkImageView src_image_views[2] = {view, view};
	VkSampler src_samplers[2] = {sampler, sampler};

	struct xrt_normalized_rect src_norm_rects[2] = {
	    {
	        // Left, takes up half the screen.
	        .x = 0.0f,
	        .y = 0.0f,
	        .w = 0.5f,
	        .h = 1.0f,
	    },
	    {
	        // Right, takes up half the screen.
	        .x = 0.5f,
	        .y = 0.0f,
	        .w = 0.5f,
	        .h = 1.0f,
	    },
	};

	render_compute_projection( //
	    crc,                   //
	    src_samplers,          //
	    src_image_views,       //
	    src_norm_rects,        //
	    target_image,          //
	    target_image_view,     //
	    views                  //
	);
}

static void
do_projection_layers(struct comp_renderer *r,
                     struct render_compute *crc,
                     const struct comp_layer *layer,
                     const struct xrt_layer_projection_view_data *lvd,
                     const struct xrt_layer_projection_view_data *rvd)
{
	const struct xrt_layer_data *data = &layer->data;
	uint32_t left_array_index = lvd->sub.array_index;
	uint32_t right_array_index = rvd->sub.array_index;
	const struct comp_swapchain_image *left = &layer->sc_array[0]->images[lvd->sub.image_index];
	const struct comp_swapchain_image *right = &layer->sc_array[1]->images[rvd->sub.image_index];

	struct render_viewport_data views[2];
	calc_viewport_data(r, &views[0], &views[1]);

	VkImage target_image = r->c->target->images[r->acquired_buffer].handle;
	VkImageView target_image_view = r->c->target->images[r->acquired_buffer].view;

	struct xrt_pose new_world_poses[2];
	struct xrt_pose unused[2]; // New eye poses, unused.
	get_view_poses(r, new_world_poses, unused);

	VkSampler clamp_to_border_black = crc->r->samplers.clamp_to_border_black;
	VkSampler src_samplers[2] = {
	    clamp_to_border_black,
	    clamp_to_border_black,
	};

	VkImageView src_image_views[2] = {
	    get_image_view(left, data->flags, left_array_index),
	    get_image_view(right, data->flags, right_array_index),
	};

	struct xrt_normalized_rect src_norm_rects[2] = {lvd->sub.norm_rect, rvd->sub.norm_rect};
	if (data->flip_y) {
		src_norm_rects[0].h = -src_norm_rects[0].h;
		src_norm_rects[0].y = 1 + src_norm_rects[0].y;
		src_norm_rects[1].h = -src_norm_rects[1].h;
		src_norm_rects[1].y = 1 + src_norm_rects[1].y;
	}

	struct xrt_pose src_poses[2] = {
	    lvd->pose,
	    rvd->pose,
	};

	struct xrt_fov src_fovs[2] = {
	    lvd->fov,
	    rvd->fov,
	};

	if (r->c->debug.atw_off) {
		render_compute_projection( //
		    crc,                   //
		    src_samplers,          //
		    src_image_views,       //
		    src_norm_rects,        //
		    target_image,          //
		    target_image_view,     //
		    views);                //
	} else {
		render_compute_projection_timewarp( //
		    crc,                            //
		    src_samplers,                   //
		    src_image_views,                //
		    src_norm_rects,                 //
		    src_poses,                      //
		    src_fovs,                       //
		    new_world_poses,                //
		    target_image,                   //
		    target_image_view,              //
		    views);                         //
	}
}

/*!
 * @pre render_compute_init(crc, &c->nr)
 */
static void
dispatch_compute(struct comp_renderer *r, struct render_compute *crc)
{
	COMP_TRACE_MARKER();

	struct comp_compositor *c = r->c;
	struct comp_target *ct = c->target;

	render_compute_begin(crc);

	struct render_viewport_data views[2];
	calc_viewport_data(r, &views[0], &views[1]);

	VkImage target_image = r->c->target->images[r->acquired_buffer].handle;
	VkImageView target_image_view = r->c->target->images[r->acquired_buffer].view;

	uint32_t layer_count = c->base.slot.layer_count;
	bool fast_path = c->base.slot.one_projection_layer_fast_path;

	if (fast_path && c->base.slot.layers[0].data.type == XRT_LAYER_STEREO_PROJECTION) {
		int i = 0;
		const struct comp_layer *layer = &c->base.slot.layers[i];
		const struct xrt_layer_stereo_projection_data *stereo = &layer->data.stereo;
		const struct xrt_layer_projection_view_data *lvd = &stereo->l;
		const struct xrt_layer_projection_view_data *rvd = &stereo->r;

		do_projection_layers(r, crc, layer, lvd, rvd);
	} else if (fast_path && c->base.slot.layers[0].data.type == XRT_LAYER_STEREO_PROJECTION_DEPTH) {
		int i = 0;
		const struct comp_layer *layer = &c->base.slot.layers[i];
		const struct xrt_layer_stereo_projection_depth_data *stereo = &layer->data.stereo_depth;
		const struct xrt_layer_projection_view_data *lvd = &stereo->l;
		const struct xrt_layer_projection_view_data *rvd = &stereo->r;

		do_projection_layers(r, crc, layer, lvd, rvd);
	} else if (layer_count > 0) {
		do_layers(r, crc, c->base.slot.layers, layer_count);

		do_distortion(r, crc, views);
	} else {
		render_compute_clear(  //
		    crc,               //
		    target_image,      //
		    target_image_view, //
		    views);            //
	}

	render_compute_end(crc);

	comp_target_mark_submit(ct, c->frame.rendering.id, os_monotonic_get_ns());

	renderer_submit_queue(r, crc->r->cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}


/*
 *
 * Interface functions.
 *
 */

void
comp_renderer_set_quad_layer(struct comp_renderer *r,
                             uint32_t layer,
                             struct comp_swapchain_image *image,
                             struct xrt_layer_data *data)
{
	struct comp_render_layer *l = r->lr->layers[layer];

	l->transformation_ubo_binding = r->lr->transformation_ubo_binding;
	l->texture_binding = r->lr->texture_binding;

	VkSampler clamp_to_edge = r->c->nr.samplers.clamp_to_edge;
	VkImageView image_view = get_image_view( //
	    image,                               //
	    data->flags,                         //
	    data->quad.sub.array_index);         //

	comp_layer_update_descriptors( //
	    l,                         //
	    clamp_to_edge,             //
	    image_view);               //

	struct xrt_vec3 s = {data->quad.size.x, data->quad.size.y, 1.0f};
	struct xrt_matrix_4x4 model_matrix;
	math_matrix_4x4_model(&data->quad.pose, &s, &model_matrix);

	comp_layer_set_model_matrix(r->lr->layers[layer], &model_matrix);

	comp_layer_set_flip_y(r->lr->layers[layer], data->flip_y);

	l->type = XRT_LAYER_QUAD;
	l->visibility = data->quad.visibility;
	l->flags = data->flags;
	l->view_space = (data->flags & XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT) != 0;

	for (uint32_t i = 0; i < 2; i++) {
		l->transformation[i].offset = data->quad.sub.rect.offset;
		l->transformation[i].extent = data->quad.sub.rect.extent;
	}
}

void
comp_renderer_set_cylinder_layer(struct comp_renderer *r,
                                 uint32_t layer,
                                 struct comp_swapchain_image *image,
                                 struct xrt_layer_data *data)
{
	struct comp_render_layer *l = r->lr->layers[layer];

	l->transformation_ubo_binding = r->lr->transformation_ubo_binding;
	l->texture_binding = r->lr->texture_binding;

	l->type = XRT_LAYER_CYLINDER;
	l->visibility = data->cylinder.visibility;
	l->flags = data->flags;
	l->view_space = (data->flags & XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT) != 0;

	// skip "infinite cylinder"
	if (data->cylinder.radius == 0.f || data->cylinder.aspect_ratio == INFINITY) {
		/* skipping the descriptor set update means the renderer must
		 * entirely skip rendering of invisible layer */
		l->visibility = XRT_LAYER_EYE_VISIBILITY_NONE;
		return;
	}

	VkSampler clamp_to_edge = r->c->nr.samplers.clamp_to_edge;
	VkImageView image_view = get_image_view( //
	    image,                               //
	    data->flags,                         //
	    data->cylinder.sub.array_index);     //

	comp_layer_update_descriptors( //
	    r->lr->layers[layer],      //
	    clamp_to_edge,             //
	    image_view);               //

	float height = (data->cylinder.radius * data->cylinder.central_angle) / data->cylinder.aspect_ratio;

	// scale unit cylinder to diameter
	float diameter = data->cylinder.radius * 2;
	struct xrt_vec3 scale = {diameter, height, diameter};
	struct xrt_matrix_4x4 model_matrix;
	math_matrix_4x4_model(&data->cylinder.pose, &scale, &model_matrix);

	comp_layer_set_model_matrix(r->lr->layers[layer], &model_matrix);

	comp_layer_set_flip_y(r->lr->layers[layer], data->flip_y);

	for (uint32_t i = 0; i < 2; i++) {
		l->transformation[i].offset = data->cylinder.sub.rect.offset;
		l->transformation[i].extent = data->cylinder.sub.rect.extent;
	}

	comp_layer_update_cylinder_vertex_buffer(l, data->cylinder.central_angle);
}

void
comp_renderer_set_projection_layer(struct comp_renderer *r,
                                   uint32_t layer,
                                   struct comp_swapchain_image *left_image,
                                   struct comp_swapchain_image *right_image,
                                   struct xrt_layer_data *data)
{
	uint32_t left_array_index = data->stereo.l.sub.array_index;
	uint32_t right_array_index = data->stereo.r.sub.array_index;

	struct comp_render_layer *l = r->lr->layers[layer];

	l->transformation_ubo_binding = r->lr->transformation_ubo_binding;
	l->texture_binding = r->lr->texture_binding;

	VkSampler clamp_to_border_black = r->c->nr.samplers.clamp_to_border_black;

	VkImageView left_image_view = get_image_view( //
	    left_image,                               //
	    data->flags,                              //
	    left_array_index);                        //

	VkImageView right_image_view = get_image_view( //
	    right_image,                               //
	    data->flags,                               //
	    right_array_index);                        //

	comp_layer_update_stereo_descriptors( //
	    l,                                //
	    clamp_to_border_black,            //
	    clamp_to_border_black,            //
	    left_image_view,                  //
	    right_image_view);                //

	comp_layer_set_flip_y(l, data->flip_y);

	l->type = XRT_LAYER_STEREO_PROJECTION;
	l->flags = data->flags;
	l->view_space = (data->flags & XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT) != 0;

	l->transformation[0].offset = data->stereo.l.sub.rect.offset;
	l->transformation[0].extent = data->stereo.l.sub.rect.extent;
	l->transformation[1].offset = data->stereo.r.sub.rect.offset;
	l->transformation[1].extent = data->stereo.r.sub.rect.extent;
}

#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT1
void
comp_renderer_set_equirect1_layer(struct comp_renderer *r,
                                  uint32_t layer,
                                  struct comp_swapchain_image *image,
                                  struct xrt_layer_data *data)
{

	struct xrt_vec3 s = {1.0f, 1.0f, 1.0f};
	struct xrt_matrix_4x4 model_matrix;
	math_matrix_4x4_model(&data->equirect1.pose, &s, &model_matrix);

	comp_layer_set_flip_y(r->lr->layers[layer], data->flip_y);

	struct comp_render_layer *l = r->lr->layers[layer];
	l->type = XRT_LAYER_EQUIRECT1;
	l->visibility = data->equirect1.visibility;
	l->flags = data->flags;
	l->view_space = (data->flags & XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT) != 0;
	l->transformation_ubo_binding = r->lr->transformation_ubo_binding;
	l->texture_binding = r->lr->texture_binding;

	VkSampler repeat = r->c->nr.samplers.repeat;
	VkImageView image_view = get_image_view( //
	    image,                               //
	    data->flags,                         //
	    data->equirect1.sub.array_index);    //

	comp_layer_update_descriptors( //
	    l,                         //
	    repeat,                    //
	    image_view);               //

	comp_layer_update_equirect1_descriptor(l, &data->equirect1);

	for (uint32_t i = 0; i < 2; i++) {
		l->transformation[i].offset = data->equirect1.sub.rect.offset;
		l->transformation[i].extent = data->equirect1.sub.rect.extent;
	}
}
#endif

#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT2
void
comp_renderer_set_equirect2_layer(struct comp_renderer *r,
                                  uint32_t layer,
                                  struct comp_swapchain_image *image,
                                  struct xrt_layer_data *data)
{

	struct xrt_vec3 s = {1.0f, 1.0f, 1.0f};
	struct xrt_matrix_4x4 model_matrix;
	math_matrix_4x4_model(&data->equirect2.pose, &s, &model_matrix);

	comp_layer_set_flip_y(r->lr->layers[layer], data->flip_y);

	struct comp_render_layer *l = r->lr->layers[layer];
	l->type = XRT_LAYER_EQUIRECT2;
	l->visibility = data->equirect2.visibility;
	l->flags = data->flags;
	l->view_space = (data->flags & XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT) != 0;
	l->transformation_ubo_binding = r->lr->transformation_ubo_binding;
	l->texture_binding = r->lr->texture_binding;

	VkSampler repeat = r->c->nr.samplers.repeat;
	VkImageView image_view = get_image_view( //
	    image,                               //
	    data->flags,                         //
	    data->equirect2.sub.array_index);    //

	comp_layer_update_descriptors( //
	    l,                         //
	    repeat,                    //
	    image_view);               //

	comp_layer_update_equirect2_descriptor(l, &data->equirect2);

	for (uint32_t i = 0; i < 2; i++) {
		l->transformation[i].offset = data->equirect2.sub.rect.offset;
		l->transformation[i].extent = data->equirect2.sub.rect.extent;
	}
}
#endif

#ifdef XRT_FEATURE_OPENXR_LAYER_CUBE
void
comp_renderer_set_cube_layer(struct comp_renderer *r,
                             uint32_t layer,
                             struct comp_swapchain_image *image,
                             struct xrt_layer_data *data)
{

	struct xrt_vec3 s = {1.0f, 1.0f, 1.0f};
	struct xrt_matrix_4x4 model_matrix;
	math_matrix_4x4_model(&data->cube.pose, &s, &model_matrix);

	comp_layer_set_flip_y(r->lr->layers[layer], data->flip_y);

	struct comp_render_layer *l = r->lr->layers[layer];
	l->type = XRT_LAYER_CUBE;
	l->visibility = data->cube.visibility;
	l->flags = data->flags;
	l->view_space = (data->flags & XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT) != 0;
	l->transformation_ubo_binding = r->lr->transformation_ubo_binding;
	l->texture_binding = r->lr->texture_binding;

	VkSampler repeat = r->c->nr.samplers.repeat;
	VkImageView image_view = get_image_view( //
	    image,                               //
	    data->flags,                         //
	    data->cube.sub.array_index);         //

	comp_layer_update_descriptors( //
	    l,                         //
	    repeat,                    //
	    image_view);               //
}
#endif

void
comp_renderer_draw(struct comp_renderer *r)
{
	COMP_TRACE_MARKER();

	struct comp_target *ct = r->c->target;
	struct comp_compositor *c = r->c;

	// Check that we don't have any bad data.
	assert(!comp_frame_is_invalid_locked(&c->frame.waited));
	assert(comp_frame_is_invalid_locked(&c->frame.rendering));

	// Move waited frame to rendering frame, clear waited.
	comp_frame_move_and_clear_locked(&c->frame.rendering, &c->frame.waited);

	// Tell the target we are starting to render, for frame timing.
	comp_target_mark_begin(ct, c->frame.rendering.id, os_monotonic_get_ns());

	// Are we ready to render? No - skip rendering.
	if (!comp_target_check_ready(r->c->target)) {
		// Need to emulate rendering for the timing.
		//! @todo This should be discard.
		comp_target_mark_submit(ct, c->frame.rendering.id, os_monotonic_get_ns());

		// Clear the rendering frame.
		comp_frame_clear_locked(&c->frame.rendering);
		return;
	}

	comp_target_flush(ct);

	comp_target_update_timings(ct);

	if (r->acquired_buffer < 0) {
		// Ensures that renderings are created.
		renderer_acquire_swapchain_image(r);
	}

	comp_target_update_timings(ct);

	bool use_compute = r->settings->use_compute;
	struct render_gfx rr = {0};
	struct render_compute crc = {0};
	if (use_compute) {
		render_compute_init(&crc, &c->nr);
		dispatch_compute(r, &crc);
	} else {
		render_gfx_init(&rr, &c->nr);
		dispatch_graphics(r, &rr);
	}

#ifdef XRT_FEATURE_WINDOW_PEEK
	if (c->peek) {
		switch (comp_window_peek_get_eye(c->peek)) {
		case COMP_WINDOW_PEEK_EYE_LEFT:
			comp_window_peek_blit(c->peek, r->lr->framebuffers[0].image, r->lr->extent.width,
			                      r->lr->extent.height);
			break;
		case COMP_WINDOW_PEEK_EYE_RIGHT:
			comp_window_peek_blit(c->peek, r->lr->framebuffers[1].image, r->lr->extent.width,
			                      r->lr->extent.height);
			break;
		case COMP_WINDOW_PEEK_EYE_BOTH:
			/* TODO: display the undistorted image */
			comp_window_peek_blit(c->peek, c->target->images[r->acquired_buffer].handle, c->target->width,
			                      c->target->height);
			break;
		}
	}
#endif

	renderer_present_swapchain_image(r, c->frame.rendering.desired_present_time_ns,
	                                 c->frame.rendering.present_slop_ns);

	// Save for timestamps below.
	uint64_t frame_id = c->frame.rendering.id;
	uint64_t desired_present_time_ns = c->frame.rendering.desired_present_time_ns;
	uint64_t predicted_display_time_ns = c->frame.rendering.predicted_display_time_ns;

	// Clear the rendered frame.
	comp_frame_clear_locked(&c->frame.rendering);

	comp_mirror_fixup_ui_state(&r->mirror_to_debug_gui, c);
	if (comp_mirror_is_ready_and_active(&r->mirror_to_debug_gui, c, predicted_display_time_ns)) {

		// Used for both, want clamp to edge to no bring in black.
		VkSampler clamp_to_edge = c->nr.samplers.clamp_to_edge;

		if (use_compute) {
			// Covers only the first half of the view.
			struct xrt_normalized_rect rect = {0, 0, 0.5f, 1.0f};

			comp_mirror_do_blit(               //
			    &r->mirror_to_debug_gui,       //
			    &c->base.vk,                   //
			    predicted_display_time_ns,     //
			    c->nr.scratch.color.image,     //
			    c->nr.scratch.color.srgb_view, //
			    clamp_to_edge,                 //
			    c->nr.scratch.extent,          //
			    rect);                         //
		} else {
			// Covers the whole view.
			struct xrt_normalized_rect rect = {0, 0, 1.0f, 1.0f};

			comp_mirror_do_blit(              //
			    &r->mirror_to_debug_gui,      //
			    &c->base.vk,                  //
			    predicted_display_time_ns,    //
			    r->lr->framebuffers[0].image, //
			    r->lr->framebuffers[0].view,  //
			    clamp_to_edge,                //
			    r->lr->extent,                //
			    rect);                        //
		}
	}

	/*
	 * This fixes a lot of validation issues as it makes sure that the
	 * command buffer has completed and all resources referred by it can
	 * now be manipulated.
	 *
	 * This is done after a swap so isn't time critical.
	 */
	renderer_wait_gpu_idle(r);


	/*
	 * Get timestamps of GPU work (if available).
	 */

	uint64_t gpu_start_ns, gpu_end_ns;
	if (render_resources_get_timestamps(&c->nr, &gpu_start_ns, &gpu_end_ns)) {
		uint64_t now_ns = os_monotonic_get_ns();
		comp_target_info_gpu(ct, frame_id, gpu_start_ns, gpu_end_ns, now_ns);
	}


	/*
	 * Free resources.
	 */

	if (use_compute) {
		render_compute_close(&crc);
	} else {
		render_gfx_close(&rr);
	}


	/*
	 * For direct mode this makes us wait until the last frame has been
	 * actually shown to the user, this avoids us missing that we have
	 * missed a frame and miss-predicting the next frame.
	 *
	 * Only do this if we are ready.
	 */
	if (comp_target_check_ready(r->c->target)) {
		// For estimating frame misses.
		uint64_t then_ns = os_monotonic_get_ns();

		// Do the acquire
		renderer_acquire_swapchain_image(r);

		// How long did it take?
		uint64_t now_ns = os_monotonic_get_ns();

		/*
		 * Make sure we at least waited 1ms before warning. Then check
		 * if we are more then 1ms behind when we wanted to present.
		 */
		if (then_ns + U_TIME_1MS_IN_NS < now_ns && //
		    desired_present_time_ns + U_TIME_1MS_IN_NS < now_ns) {
			uint64_t diff_ns = now_ns - desired_present_time_ns;
			double diff_ms_f = time_ns_to_ms_f(diff_ns);
			COMP_WARN(c, "Compositor probably missed frame by %.2fms", diff_ms_f);
		}
	}

	comp_target_update_timings(ct);
}

void
comp_renderer_allocate_layers(struct comp_renderer *self, uint32_t layer_count)
{
	COMP_TRACE_MARKER();

	comp_layer_renderer_allocate_layers(self->lr, layer_count);
}

void
comp_renderer_destroy_layers(struct comp_renderer *self)
{
	COMP_TRACE_MARKER();

	comp_layer_renderer_destroy_layers(self->lr);
}

struct comp_renderer *
comp_renderer_create(struct comp_compositor *c)
{
	struct comp_renderer *r = U_TYPED_CALLOC(struct comp_renderer);

	renderer_init(r, c);

	return r;
}

void
comp_renderer_destroy(struct comp_renderer **ptr_r)
{
	if (ptr_r == NULL) {
		return;
	}

	struct comp_renderer *r = *ptr_r;
	if (r == NULL) {
		return;
	}

	renderer_fini(r);

	free(r);
	*ptr_r = NULL;
}

void
comp_renderer_add_debug_vars(struct comp_renderer *self)
{
	struct comp_renderer *r = self;

	comp_mirror_add_debug_vars(&r->mirror_to_debug_gui, r->c);
}
