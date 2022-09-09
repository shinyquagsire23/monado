// Copyright 2019-2022, Collabora, Ltd.
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

#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_compositor.h"

#include "os/os_time.h"

#include "math/m_api.h"
#include "math/m_vec3.h"
#include "math/m_matrix_4x4_f64.h"
#include "math/m_space.h"

#include "util/u_misc.h"
#include "util/u_trace_marker.h"
#include "util/u_distortion_mesh.h"
#include "util/u_sink.h"
#include "util/u_var.h"
#include "util/u_frame.h"
#include "util/u_frame_times_widget.h"

#include "main/comp_layer_renderer.h"

#ifdef XRT_FEATURE_WINDOW_PEEK
#include "main/comp_window_peek.h"
#endif

#include "vk/vk_helpers.h"
#include "vk/vk_image_readback_to_xf_pool.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>


/*
 *
 * Private struct.
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

	struct
	{
		// Hint: enable/disable is in c->mirroring_to_debug_gui. It's there because comp_renderer is just a
		// forward decl to comp_compositor.c

		struct u_frame_times_widget push_frame_times;

		float target_frame_time_ms;
		uint64_t last_push_ts_ns;
		int push_every_frame_out_of_X;

		struct u_sink_debug debug_sink;
		VkExtent2D image_extent;
		uint64_t sequence;

		struct vk_image_readback_to_xf_pool *pool;

	} mirror_to_debug_gui;


	struct
	{
		VkSemaphore present_complete;
		VkSemaphore render_complete;
	} semaphores;
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
renderer_init_semaphores(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->base.vk;
	VkResult ret;

	VkSemaphoreCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	ret = vk->vkCreateSemaphore(vk->device, &info, NULL, &r->semaphores.present_complete);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}

	ret = vk->vkCreateSemaphore(vk->device, &info, NULL, &r->semaphores.render_complete);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkCreateSemaphore: %s", vk_result_string(ret));
	}
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

//! @pre comp_target_has_images(r->c->target)
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
	 * Init
	 */

	render_gfx_init(rr, &c->nr);
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
		math_matrix_2x2_multiply(&distortion_data[0].vertex_rot,  //
		                         &rotation_90_cw,                 //
		                         &distortion_data[0].vertex_rot); //
		math_matrix_2x2_multiply(&distortion_data[1].vertex_rot,  //
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
	if (!render_ensure_distortion_buffer(&r->c->nr, &r->c->base.vk, r->c->xdev, pre_rotate))
		return false;

	r->buffer_count = r->c->target->image_count;

	renderer_create_layer_renderer(r);
	renderer_create_renderings_and_fences(r);

	assert(r->buffer_count != 0);

	return true;
}

//! Create renderer and initialize non-image-dependent members
static void
renderer_create(struct comp_renderer *r, struct comp_compositor *c)
{
	r->c = c;
	r->settings = &c->settings;

	r->acquired_buffer = -1;
	r->fenced_buffer = -1;
	r->semaphores.present_complete = VK_NULL_HANDLE;
	r->semaphores.render_complete = VK_NULL_HANDLE;
	r->rtr_array = NULL;

	renderer_init_semaphores(r);

	// Try to early-allocate these, in case we can.
	renderer_ensure_images_and_renderings(r, false);

	double orig_width = r->lr->extent.width;
	double orig_height = r->lr->extent.height;

	double target_height = 1080;

	double mul = target_height / orig_height;

	// Casts seem to always round down; we don't want that here.
	r->mirror_to_debug_gui.image_extent.width = (uint32_t)(round(orig_width * mul));
	r->mirror_to_debug_gui.image_extent.height = (uint32_t)target_height;


	// We want the images to have even widths/heights so that libx264 can encode them properly; no other reason.
	if (r->mirror_to_debug_gui.image_extent.width % 2 == 1) {
		r->mirror_to_debug_gui.image_extent.width += 1;
	}

	u_sink_debug_init(&r->mirror_to_debug_gui.debug_sink);

	struct vk_bundle *vk = &r->c->base.vk;

	vk_image_readback_to_xf_pool_create(vk, r->mirror_to_debug_gui.image_extent, &r->mirror_to_debug_gui.pool,
	                                    XRT_FORMAT_R8G8B8X8);
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


#define WAIT_SEMAPHORE_COUNT 1
	VkPipelineStageFlags stage_flags[WAIT_SEMAPHORE_COUNT] = {pipeline_stage_flag};
	VkSemaphore wait_semaphores[WAIT_SEMAPHORE_COUNT] = {r->semaphores.present_complete};

	// Wait for the last fence, if any.
	renderer_wait_for_last_fence(r);
	assert(r->fenced_buffer < 0);

	assert(r->acquired_buffer >= 0);
	ret = vk->vkResetFences(vk->device, 1, &r->fences[r->acquired_buffer]);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkResetFences: %s", vk_result_string(ret));
	}

	VkSubmitInfo comp_submit_info = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .waitSemaphoreCount = WAIT_SEMAPHORE_COUNT,
	    .pWaitSemaphores = wait_semaphores,
	    .pWaitDstStageMask = stage_flags,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &cmd,
	    .signalSemaphoreCount = 1,
	    .pSignalSemaphores = &r->semaphores.render_complete,
	};

	ret = vk_locked_submit(vk, vk->queue, 1, &comp_submit_info, r->fences[r->acquired_buffer]);
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

	struct xrt_space_relation head_relation = XRT_SPACE_RELATION_ZERO;
	struct xrt_fov fovs[2] = {0};
	struct xrt_pose poses[2] = {0};

	struct xrt_vec3 default_eye_relation = {
	    0.063000f, /*! @todo get actual ipd_meters */
	    0.0f,
	    0.0f,
	};

	xrt_device_get_view_poses(                           //
	    r->c->xdev,                                      //
	    &default_eye_relation,                           //
	    r->c->frame.rendering.predicted_display_time_ns, //
	    2,                                               //
	    &head_relation,                                  //
	    fovs,                                            //
	    poses);                                          //

	struct xrt_pose base_space_pose = XRT_POSE_IDENTITY;

	for (uint32_t i = 0; i < 2; i++) {
		const struct xrt_fov fov = fovs[i];
		const struct xrt_pose eye_pose = poses[i];

		comp_layer_renderer_set_fov(r->lr, &fov, i);

		struct xrt_space_relation result = {0};
		struct xrt_relation_chain xrc = {0};
		m_relation_chain_push_pose_if_not_identity(&xrc, &eye_pose);
		m_relation_chain_push_relation(&xrc, &head_relation);
		m_relation_chain_push_pose_if_not_identity(&xrc, &base_space_pose);
		m_relation_chain_resolve(&xrc, &result);

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
	ret = comp_target_acquire(r->c->target, r->semaphores.present_complete, &buffer_index);

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
		ret = comp_target_acquire(r->c->target, r->semaphores.present_complete, &buffer_index);
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

	ret = comp_target_present(         //
	    r->c->target,                  //
	    r->c->base.vk.queue,           //
	    r->acquired_buffer,            //
	    r->semaphores.render_complete, //
	    desired_present_time_ns,       //
	    present_slop_ns);              //
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
renderer_destroy(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->base.vk;

	// Left eye readback
	vk_image_readback_to_xf_pool_destroy(vk, &r->mirror_to_debug_gui.pool);

	u_sink_debug_destroy(&r->mirror_to_debug_gui.debug_sink);

	// Command buffers
	renderer_close_renderings_and_fences(r);

	// Semaphores
	if (r->semaphores.present_complete != VK_NULL_HANDLE) {
		vk->vkDestroySemaphore(vk->device, r->semaphores.present_complete, NULL);
		r->semaphores.present_complete = VK_NULL_HANDLE;
	}
	if (r->semaphores.render_complete != VK_NULL_HANDLE) {
		vk->vkDestroySemaphore(vk->device, r->semaphores.render_complete, NULL);
		r->semaphores.render_complete = VK_NULL_HANDLE;
	}

	comp_layer_renderer_destroy(&(r->lr));

	u_var_remove_root(r);
	u_frame_times_widget_teardown(&r->mirror_to_debug_gui.push_frame_times);
}

static VkImageView
get_image_view(const struct comp_swapchain_image *image, enum xrt_layer_composition_flags flags, uint32_t array_index)
{
	if (flags & XRT_LAYER_COMPOSITION_BLEND_TEXTURE_SOURCE_ALPHA_BIT) {
		return image->views.alpha[array_index];
	}

	return image->views.no_alpha[array_index];
}

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

	VkSampler src_samplers[2] = {
	    left->sampler,
	    right->sampler,
	};

	VkImageView src_image_views[2] = {
	    get_image_view(left, data->flags, left_array_index),
	    get_image_view(right, data->flags, right_array_index),
	};

	renderer_build_rendering(r, rr, rts, src_samplers, src_image_views, src_norm_rects);
}

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

		VkSampler src_samplers[2] = {
		    r->lr->framebuffers[0].sampler,
		    r->lr->framebuffers[1].sampler,

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

		do_gfx_mesh_and_proj(r, rr, rtr, layer, lvd, rvd);

		renderer_submit_queue(r, rr->r->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		// We mark afterwards to not include CPU time spent.
		comp_target_mark_submit(ct, c->frame.rendering.id, os_monotonic_get_ns());
	} break;

	case XRT_LAYER_STEREO_PROJECTION_DEPTH: {
		const struct xrt_layer_stereo_projection_depth_data *stereo = &layer->data.stereo_depth;
		const struct xrt_layer_projection_view_data *lvd = &stereo->l;
		const struct xrt_layer_projection_view_data *rvd = &stereo->r;

		do_gfx_mesh_and_proj(r, rr, rtr, layer, lvd, rvd);

		renderer_submit_queue(r, rr->r->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

		// We mark afterwards to not include CPU time spent.
		comp_target_mark_submit(ct, c->frame.rendering.id, os_monotonic_get_ns());
	} break;

	default: assert(false);
	}
}


/*
 *
 * Compute
 *
 */

static void
get_view_poses(struct comp_renderer *r, struct xrt_pose out_results[2])
{
	COMP_TRACE_MARKER();

	struct xrt_space_relation head_relation = XRT_SPACE_RELATION_ZERO;
	struct xrt_fov fovs[2] = {0};
	struct xrt_pose poses[2] = {0};

	struct xrt_vec3 default_eye_relation = {
	    0.063000f, /*! @todo get actual ipd_meters */
	    0.0f,
	    0.0f,
	};

	xrt_device_get_view_poses(                           //
	    r->c->xdev,                                      //
	    &default_eye_relation,                           //
	    r->c->frame.rendering.predicted_display_time_ns, //
	    2,                                               //
	    &head_relation,                                  //
	    fovs,                                            //
	    poses);                                          //

	struct xrt_pose base_space_pose = XRT_POSE_IDENTITY;
	for (uint32_t i = 0; i < 2; i++) {
		const struct xrt_fov fov = fovs[i];
		const struct xrt_pose eye_pose = poses[i];

		comp_layer_renderer_set_fov(r->lr, &fov, i);

		struct xrt_space_relation result = {0};
		struct xrt_relation_chain xrc = {0};
		m_relation_chain_push_pose_if_not_identity(&xrc, &eye_pose);
		m_relation_chain_push_relation(&xrc, &head_relation);
		m_relation_chain_push_pose_if_not_identity(&xrc, &base_space_pose);
		m_relation_chain_resolve(&xrc, &result);

		out_results[i] = result.pose;
	}
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

	struct xrt_pose new_view_poses[2];
	get_view_poses(r, new_view_poses);

	VkSampler src_samplers[2] = {
	    left->sampler,
	    right->sampler,
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
		    new_view_poses,                 //
		    target_image,                   //
		    target_image_view,              //
		    views);                         //
	}
}

static void
dispatch_compute(struct comp_renderer *r, struct render_compute *crc)
{
	COMP_TRACE_MARKER();

	struct comp_compositor *c = r->c;
	struct comp_target *ct = c->target;

	render_compute_init(crc, &c->nr);
	render_compute_begin(crc);

	struct render_viewport_data views[2];
	calc_viewport_data(r, &views[0], &views[1]);

	VkImage target_image = r->c->target->images[r->acquired_buffer].handle;
	VkImageView target_image_view = r->c->target->images[r->acquired_buffer].view;

	uint32_t layer_count = c->base.slot.layer_count;
	if (layer_count > 0 && c->base.slot.layers[0].data.type == XRT_LAYER_STEREO_PROJECTION) {
		int i = 0;
		const struct comp_layer *layer = &c->base.slot.layers[i];
		const struct xrt_layer_stereo_projection_data *stereo = &layer->data.stereo;
		const struct xrt_layer_projection_view_data *lvd = &stereo->l;
		const struct xrt_layer_projection_view_data *rvd = &stereo->r;

		do_projection_layers(r, crc, layer, lvd, rvd);
	} else if (layer_count > 0 && c->base.slot.layers[0].data.type == XRT_LAYER_STEREO_PROJECTION_DEPTH) {
		int i = 0;
		const struct comp_layer *layer = &c->base.slot.layers[i];
		const struct xrt_layer_stereo_projection_depth_data *stereo = &layer->data.stereo_depth;
		const struct xrt_layer_projection_view_data *lvd = &stereo->l;
		const struct xrt_layer_projection_view_data *rvd = &stereo->r;

		do_projection_layers(r, crc, layer, lvd, rvd);
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

	comp_layer_update_descriptors(l, image->sampler,
	                              get_image_view(image, data->flags, data->quad.sub.array_index));

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

	comp_layer_update_descriptors(r->lr->layers[layer], image->sampler,
	                              get_image_view(image, data->flags, data->cylinder.sub.array_index));


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

	comp_layer_update_stereo_descriptors(l, left_image->sampler, right_image->sampler,
	                                     get_image_view(left_image, data->flags, left_array_index),
	                                     get_image_view(right_image, data->flags, right_array_index));

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

	comp_layer_update_descriptors(l, image->repeat_sampler,
	                              get_image_view(image, data->flags, data->equirect1.sub.array_index));

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

	comp_layer_update_descriptors(l, image->repeat_sampler,
	                              get_image_view(image, data->flags, data->equirect2.sub.array_index));

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

	comp_layer_update_descriptors(l, image->repeat_sampler,
	                              get_image_view(image, data->flags, data->cube.sub.array_index));
}
#endif

static void
mirror_to_debug_gui_fixup_ui_state(struct comp_renderer *r)
{
	// One out of every zero frames is not what we want!
	// Also one out of every negative two frames, etc. is nonsensical
	if (r->mirror_to_debug_gui.push_every_frame_out_of_X < 1) {
		r->mirror_to_debug_gui.push_every_frame_out_of_X = 1;
	}

	r->mirror_to_debug_gui.target_frame_time_ms = (float)r->mirror_to_debug_gui.push_every_frame_out_of_X *
	                                              (float)time_ns_to_ms_f(r->c->settings.nominal_frame_interval_ns);

	r->mirror_to_debug_gui.push_frame_times.debug_var->reference_timing =
	    r->mirror_to_debug_gui.target_frame_time_ms;
	r->mirror_to_debug_gui.push_frame_times.debug_var->range = r->mirror_to_debug_gui.target_frame_time_ms;
}

static bool
can_mirror_to_debug_gui(struct comp_renderer *r)
{
	if (!r->c->mirroring_to_debug_gui || !u_sink_debug_is_active(&r->mirror_to_debug_gui.debug_sink)) {
		return false;
	}

	uint64_t now = r->c->frame.rendering.predicted_display_time_ns;

	double diff_s = (double)(now - r->mirror_to_debug_gui.last_push_ts_ns) / (double)U_TIME_1MS_IN_NS;

	// Completely unscientific - lower values probably works fine too.
	// I figure we don't have very many 500Hz displays and this woorks great for 120-144hz
	double slop_ms = 2;

	if (diff_s < r->mirror_to_debug_gui.target_frame_time_ms - slop_ms) {
		return false;
	}
	r->mirror_to_debug_gui.last_push_ts_ns = now;
	return true;
}

static void
mirror_to_debug_gui_do_blit(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->base.vk;
	VkResult ret;

	struct vk_image_readback_to_xf *wrap = NULL;

	if (!vk_image_readback_to_xf_pool_get_unused_frame(vk, r->mirror_to_debug_gui.pool, &wrap)) {
		return;
	}

	VkCommandBuffer cmd;
	vk_cmd_buffer_create_and_begin(vk, &cmd);

	// For submitting commands.
	os_mutex_lock(&vk->cmd_pool_mutex);

	VkImage copy_from = r->lr->framebuffers[0].image;

	VkImageSubresourceRange first_color_level_subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = 1,
	};

	// Barrier to make destination a destination
	vk_cmd_image_barrier_locked(              //
	    vk,                                   // vk_bundle
	    cmd,                                  // cmdbuffer
	    wrap->image,                          // image
	    VK_ACCESS_HOST_READ_BIT,              // srcAccessMask
	    VK_ACCESS_TRANSFER_WRITE_BIT,         // dstAccessMask
	    wrap->layout,                         // oldImageLayout
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // newImageLayout
	    VK_PIPELINE_STAGE_HOST_BIT,           // srcStageMask
	    VK_PIPELINE_STAGE_TRANSFER_BIT,       // dstStageMask
	    first_color_level_subresource_range); // subresourceRange

	// Barrier to make source a source
	vk_cmd_image_barrier_locked(                  //
	    vk,                                       // vk_bundle
	    cmd,                                      // cmdbuffer
	    copy_from,                                // image
	    VK_ACCESS_SHADER_WRITE_BIT,               // srcAccessMask
	    VK_ACCESS_TRANSFER_READ_BIT,              // dstAccessMask
	    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // oldImageLayout
	    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,     // newImageLayout
	    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,     // srcStageMask
	    VK_PIPELINE_STAGE_TRANSFER_BIT,           // dstStageMask
	    first_color_level_subresource_range);     // subresourceRange


	VkImageBlit blit = {0};
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.layerCount = 1;

	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.layerCount = 1;

	blit.srcOffsets[1].x = r->lr->extent.width;
	blit.srcOffsets[1].y = r->lr->extent.height;
	blit.srcOffsets[1].z = 1;


	blit.dstOffsets[1].x = r->mirror_to_debug_gui.image_extent.width;
	blit.dstOffsets[1].y = r->mirror_to_debug_gui.image_extent.height;
	blit.dstOffsets[1].z = 1;

	vk->vkCmdBlitImage(                       //
	    cmd,                                  // commandBuffer
	    copy_from,                            // srcImage
	    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // srcImageLayout
	    wrap->image,                          // dstImage
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // dstImageLayout
	    1,                                    // regionCount
	    &blit,                                // pRegions
	    VK_FILTER_LINEAR                      // filter
	);

	wrap->layout = VK_IMAGE_LAYOUT_GENERAL;

	// Reset destination
	vk_cmd_image_barrier_locked(              //
	    vk,                                   // vk_bundle
	    cmd,                                  // cmdbuffer
	    wrap->image,                          // image
	    VK_ACCESS_TRANSFER_WRITE_BIT,         // srcAccessMask
	    VK_ACCESS_HOST_READ_BIT,              // dstAccessMask
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // oldImageLayout
	    wrap->layout,                         // newImageLayout
	    VK_PIPELINE_STAGE_TRANSFER_BIT,       // srcStageMask
	    VK_PIPELINE_STAGE_HOST_BIT,           // dstStageMask
	    first_color_level_subresource_range); // subresourceRange

	// Reset src
	vk_cmd_image_barrier_locked(                  //
	    vk,                                       // vk_bundle
	    cmd,                                      // cmdbuffer
	    copy_from,                                // image
	    VK_ACCESS_TRANSFER_READ_BIT,              // srcAccessMask
	    VK_ACCESS_SHADER_WRITE_BIT,               // dstAccessMask
	    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,     // oldImageLayout
	    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // newImageLayout
	    VK_PIPELINE_STAGE_TRANSFER_BIT,           // srcStageMask
	    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,     // dstStageMask
	    first_color_level_subresource_range);     // subresourceRange

	// Done submitting commands.
	os_mutex_unlock(&vk->cmd_pool_mutex);

	// Waits for command to finish.
	ret = vk_cmd_buffer_submit(vk, cmd);
	if (ret != VK_SUCCESS) {
		//! @todo Better handling of error?
		COMP_ERROR(r->c, "Failed to mirror image");
	}

	wrap->base_frame.source_timestamp = wrap->base_frame.timestamp =
	    r->c->frame.rendering.predicted_display_time_ns;
	wrap->base_frame.source_id = r->mirror_to_debug_gui.sequence++;


	struct xrt_frame *frame = &wrap->base_frame;
	wrap = NULL;
	u_sink_debug_push_frame(&r->mirror_to_debug_gui.debug_sink, frame);
	u_frame_times_widget_push_sample(&r->mirror_to_debug_gui.push_frame_times,
	                                 r->c->frame.rendering.predicted_display_time_ns);

	xrt_frame_reference(&frame, NULL);
}

void
comp_renderer_draw(struct comp_renderer *r)
{
	COMP_TRACE_MARKER();

	struct comp_target *ct = r->c->target;
	struct comp_compositor *c = r->c;


	assert(c->frame.rendering.id == -1);

	c->frame.rendering = c->frame.waited;
	c->frame.waited.id = -1;

	comp_target_mark_begin(ct, c->frame.rendering.id, os_monotonic_get_ns());

	// Are we ready to render? No - skip rendering.
	if (!comp_target_check_ready(r->c->target)) {
		// Need to emulate rendering for the timing.
		//! @todo This should be discard.
		comp_target_mark_submit(ct, c->frame.rendering.id, os_monotonic_get_ns());
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
		dispatch_compute(r, &crc);
	} else {
		dispatch_graphics(r, &rr);
	}

#ifdef XRT_FEATURE_WINDOW_PEEK
	if (c->peek) {
		switch (c->peek->eye) {
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

	// Clear the frame.
	c->frame.rendering.id = -1;

	mirror_to_debug_gui_fixup_ui_state(r);
	if (can_mirror_to_debug_gui(r)) {
		mirror_to_debug_gui_do_blit(r);
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
		//! @todo submit data to target (pacer).
		(void)frame_id;

#define TE_BEG(TRACK, TIME, NAME) U_TRACE_EVENT_BEGIN_ON_TRACK_DATA(timing, TRACK, TIME, NAME, PERCETTO_I(frame_id))
#define TE_END(TRACK, TIME) U_TRACE_EVENT_END_ON_TRACK(timing, TRACK, TIME)

		TE_BEG(pc_gpu, gpu_start_ns, "gpu");
		TE_END(pc_gpu, gpu_end_ns);

#undef TE_BEG
#undef TE_END
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
	 */
	renderer_acquire_swapchain_image(r);

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

	renderer_create(r, c);

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

	renderer_destroy(r);
	free(r);
	*ptr_r = NULL;
}

void
comp_renderer_add_debug_vars(struct comp_renderer *self)
{
	struct comp_renderer *r = self;
	r->mirror_to_debug_gui.push_every_frame_out_of_X = 2;

	u_frame_times_widget_init(&r->mirror_to_debug_gui.push_frame_times, 0.f, 0.f);
	mirror_to_debug_gui_fixup_ui_state(r);

	u_var_add_root(r, "Readback", true);

	u_var_add_bool(r, &r->c->mirroring_to_debug_gui, "Readback left eye to debug GUI");
	u_var_add_i32(r, &r->mirror_to_debug_gui.push_every_frame_out_of_X, "Push 1 frame out of every X frames");

	u_var_add_ro_f32(r, &r->mirror_to_debug_gui.push_frame_times.fps, "FPS (Readback)");
	u_var_add_f32_timing(r, r->mirror_to_debug_gui.push_frame_times.debug_var, "Frame Times (Readback)");


	u_var_add_sink_debug(r, &r->mirror_to_debug_gui.debug_sink, "Left view!");
}
