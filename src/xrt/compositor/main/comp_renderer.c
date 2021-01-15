// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor rendering code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include "xrt/xrt_compositor.h"

#include "math/m_space.h"

#include "util/u_misc.h"
#include "util/u_distortion_mesh.h"

#include "main/comp_layer_renderer.h"
#include "math/m_api.h"

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
	uint32_t current_buffer;

	VkQueue queue;

	struct
	{
		VkSemaphore present_complete;
		VkSemaphore render_complete;
	} semaphores;

	struct comp_rendering *rrs;
	VkFence *fences;
	uint32_t num_buffers;

	struct comp_compositor *c;
	struct comp_settings *settings;

	struct comp_layer_renderer *lr;
};


/*
 *
 * Pre declare functions.
 *
 */

static void
renderer_create(struct comp_renderer *r, struct comp_compositor *c);

static void
renderer_init(struct comp_renderer *r);

static void
renderer_submit_queue(struct comp_renderer *r);

static void
renderer_build_renderings(struct comp_renderer *r);

static void
renderer_allocate_renderings(struct comp_renderer *r);

static void
renderer_close_renderings(struct comp_renderer *r);

static void
renderer_init_semaphores(struct comp_renderer *r);

static void
renderer_resize(struct comp_renderer *r);

static void
renderer_acquire_swapchain_image(struct comp_renderer *r);

static void
renderer_present_swapchain_image(struct comp_renderer *r);

static void
renderer_destroy(struct comp_renderer *r);


/*
 *
 * Interface functions.
 *
 */

struct comp_renderer *
comp_renderer_create(struct comp_compositor *c)
{
	struct comp_renderer *r = U_TYPED_CALLOC(struct comp_renderer);

	renderer_create(r, c);
	renderer_init(r);

	return r;
}

void
comp_renderer_destroy(struct comp_renderer *r)
{
	renderer_destroy(r);
	free(r);
}

/*
 *
 * Functions.
 *
 */

static void
renderer_create(struct comp_renderer *r, struct comp_compositor *c)
{
	r->c = c;
	r->settings = &c->settings;

	r->current_buffer = 0;
	r->queue = VK_NULL_HANDLE;
	r->semaphores.present_complete = VK_NULL_HANDLE;
	r->semaphores.render_complete = VK_NULL_HANDLE;

	r->rrs = NULL;
}

static void
renderer_submit_queue(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;
	VkResult ret;

	VkPipelineStageFlags stage_flags[1] = {
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	};

	ret = vk->vkWaitForFences(vk->device, 1, &r->fences[r->current_buffer], VK_TRUE, UINT64_MAX);
	if (ret != VK_SUCCESS)
		COMP_ERROR(r->c, "vkWaitForFences: %s", vk_result_string(ret));

	ret = vk->vkResetFences(vk->device, 1, &r->fences[r->current_buffer]);
	if (ret != VK_SUCCESS)
		COMP_ERROR(r->c, "vkResetFences: %s", vk_result_string(ret));

	VkSubmitInfo comp_submit_info = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .waitSemaphoreCount = 1,
	    .pWaitSemaphores = &r->semaphores.present_complete,
	    .pWaitDstStageMask = stage_flags,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &r->rrs[r->current_buffer].cmd,
	    .signalSemaphoreCount = 1,
	    .pSignalSemaphores = &r->semaphores.render_complete,
	};

	ret = vk_locked_submit(vk, r->queue, 1, &comp_submit_info, r->fences[r->current_buffer]);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkQueueSubmit: %s", vk_result_string(ret));
	}
}

static void
renderer_build_rendering(struct comp_renderer *r, struct comp_rendering *rr, uint32_t index)
{
	struct comp_compositor *c = r->c;

	struct comp_target_data data;
	data.format = r->c->target->format;
	data.is_external = true;
	data.width = r->c->target->width;
	data.height = r->c->target->height;

	bool pre_rotate = false;
	if (r->c->target->surface_transform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
	    r->c->target->surface_transform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
		COMP_DEBUG(c,
		           "Swapping width and height,"
		           "since we are pre rotating");
		pre_rotate = true;
	}


	float w = pre_rotate ? r->c->xdev->hmd->screens[0].h_pixels : r->c->xdev->hmd->screens[0].w_pixels;
	float h = pre_rotate ? r->c->xdev->hmd->screens[0].w_pixels : r->c->xdev->hmd->screens[0].h_pixels;

	float scale_x = (float)r->c->target->width / w;
	float scale_y = (float)r->c->target->height / h;

	struct xrt_view *l_v = &r->c->xdev->hmd->views[0];


	struct comp_viewport_data l_viewport_data;

	if (pre_rotate) {
		l_viewport_data = (struct comp_viewport_data){
		    .x = (uint32_t)(l_v->viewport.y_pixels * scale_x),
		    .y = (uint32_t)(l_v->viewport.x_pixels * scale_y),
		    .w = (uint32_t)(l_v->viewport.h_pixels * scale_x),
		    .h = (uint32_t)(l_v->viewport.w_pixels * scale_y),
		};
	} else {
		l_viewport_data = (struct comp_viewport_data){
		    .x = (uint32_t)(l_v->viewport.x_pixels * scale_x),
		    .y = (uint32_t)(l_v->viewport.y_pixels * scale_y),
		    .w = (uint32_t)(l_v->viewport.w_pixels * scale_x),
		    .h = (uint32_t)(l_v->viewport.h_pixels * scale_y),
		};
	}

	const struct xrt_matrix_2x2 rotation_90_cw = {{
	    .vecs =
	        {
	            {0, 1},
	            {-1, 0},
	        },
	}};


	struct comp_mesh_ubo_data l_data = {
	    .rot = l_v->rot,
	    .flip_y = false,
	};

	if (pre_rotate) {
		math_matrix_2x2_multiply(&l_v->rot, &rotation_90_cw, &l_data.rot);
	}

	struct xrt_view *r_v = &r->c->xdev->hmd->views[1];

	struct comp_viewport_data r_viewport_data;

	if (pre_rotate) {
		r_viewport_data = (struct comp_viewport_data){
		    .x = (uint32_t)(r_v->viewport.y_pixels * scale_x),
		    .y = (uint32_t)(r_v->viewport.x_pixels * scale_y),
		    .w = (uint32_t)(r_v->viewport.h_pixels * scale_x),
		    .h = (uint32_t)(r_v->viewport.w_pixels * scale_y),
		};
	} else {
		r_viewport_data = (struct comp_viewport_data){
		    .x = (uint32_t)(r_v->viewport.x_pixels * scale_x),
		    .y = (uint32_t)(r_v->viewport.y_pixels * scale_y),
		    .w = (uint32_t)(r_v->viewport.w_pixels * scale_x),
		    .h = (uint32_t)(r_v->viewport.h_pixels * scale_y),
		};
	}

	struct comp_mesh_ubo_data r_data = {
	    .rot = r_v->rot,
	    .flip_y = false,
	};

	if (pre_rotate) {
		math_matrix_2x2_multiply(&r_v->rot, &rotation_90_cw, &r_data.rot);
	}

	/*
	 * Init
	 */

	comp_rendering_init(c, &c->nr, rr);

	comp_draw_begin_target_single(        //
	    rr,                               //
	    r->c->target->images[index].view, //
	    &data);                           //


	/*
	 * Viewport one
	 */

	comp_draw_begin_view(rr,                //
	                     0,                 // target_index
	                     0,                 // view_index
	                     &l_viewport_data); // viewport_data

	comp_draw_distortion(rr,                             //
	                     r->lr->framebuffers[0].sampler, //
	                     r->lr->framebuffers[0].view,    //
	                     &l_data);                       //

	comp_draw_end_view(rr);


	/*
	 * Viewport two
	 */

	comp_draw_begin_view(rr,                //
	                     0,                 // target_index
	                     1,                 // view_index
	                     &r_viewport_data); // viewport_data

	comp_draw_distortion(rr,                             //
	                     r->lr->framebuffers[1].sampler, //
	                     r->lr->framebuffers[1].view,    //
	                     &r_data);                       //

	comp_draw_end_view(rr);


	/*
	 * End
	 */

	comp_draw_end_target(rr);
}

static void
renderer_build_renderings(struct comp_renderer *r)
{
	for (uint32_t i = 0; i < r->num_buffers; ++i) {
		renderer_build_rendering(r, &r->rrs[i], i);
	}
}

static void
renderer_create_fences(struct comp_renderer *r)
{
	r->fences = U_TYPED_ARRAY_CALLOC(VkFence, r->num_buffers);

	struct vk_bundle *vk = &r->c->vk;

	for (uint32_t i = 0; i < r->num_buffers; i++) {
		VkResult ret = vk->vkCreateFence(vk->device,
		                                 &(VkFenceCreateInfo){.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		                                                      .flags = VK_FENCE_CREATE_SIGNALED_BIT},
		                                 NULL, &r->fences[i]);
		if (ret != VK_SUCCESS) {
			COMP_ERROR(r->c, "vkCreateFence: %s", vk_result_string(ret));
		}
	}
}

static void
renderer_get_view_projection(struct comp_renderer *r)
{
	struct xrt_space_relation relation;

	xrt_device_get_tracked_pose(r->c->xdev, XRT_INPUT_GENERIC_HEAD_POSE, r->c->last_next_display_time, &relation);

	struct xrt_vec3 eye_relation = {
	    0.063000f, /* TODO: get actual ipd_meters */
	    0.0f,
	    0.0f,
	};

	struct xrt_pose base_space_pose = {
	    .position = (struct xrt_vec3){0, 0, 0},
	    .orientation = (struct xrt_quat){0, 0, 0, 1},
	};

	for (uint32_t i = 0; i < 2; i++) {
		struct xrt_fov fov = r->c->xdev->hmd->views[i].fov;

		comp_layer_renderer_set_fov(r->lr, &fov, i);

		struct xrt_pose eye_pose;
		xrt_device_get_view_pose(r->c->xdev, &eye_relation, i, &eye_pose);

		struct xrt_space_relation result = {0};
		struct xrt_space_graph xsg = {0};
		m_space_graph_add_pose_if_not_identity(&xsg, &eye_pose);
		m_space_graph_add_relation(&xsg, &relation);
		m_space_graph_add_pose_if_not_identity(&xsg, &base_space_pose);
		m_space_graph_resolve(&xsg, &result);

		comp_layer_renderer_set_pose(r->lr, &eye_pose, &result.pose, i);
	}
}

static void
renderer_init(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;

	vk->vkGetDeviceQueue(vk->device, r->c->vk.queue_family_index, 0, &r->queue);
	renderer_init_semaphores(r);
	assert(r->c->target->num_images > 0);

	r->num_buffers = r->c->target->num_images;

	renderer_create_fences(r);

	VkExtent2D extent;
	if (r->c->target->surface_transform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
	    r->c->target->surface_transform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
		// Swapping width and height, since we are pre rotating
		extent = (VkExtent2D){
		    .width = r->c->xdev->hmd->screens[0].h_pixels,
		    .height = r->c->xdev->hmd->screens[0].w_pixels,
		};
	} else {
		extent = (VkExtent2D){
		    .width = r->c->xdev->hmd->screens[0].w_pixels,
		    .height = r->c->xdev->hmd->screens[0].h_pixels,
		};
	}

	r->lr = comp_layer_renderer_create(vk, &r->c->shaders, extent, VK_FORMAT_B8G8R8A8_SRGB);

	renderer_allocate_renderings(r);
	renderer_build_renderings(r);
}

VkImageView
get_image_view(struct comp_swapchain_image *image, enum xrt_layer_composition_flags flags, uint32_t array_index)
{
	if (flags & XRT_LAYER_COMPOSITION_BLEND_TEXTURE_SOURCE_ALPHA_BIT) {
		return image->views.alpha[array_index];
	}
	return image->views.no_alpha[array_index];
}

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

void
comp_renderer_draw(struct comp_renderer *r)
{
	renderer_get_view_projection(r);
	comp_layer_renderer_draw(r->lr);

	comp_target_flush(r->c->target);

	renderer_acquire_swapchain_image(r);
	renderer_submit_queue(r);
	renderer_present_swapchain_image(r);

	/*
	 * This fixes a lot of validation issues as it makes sure that the
	 * command buffer has completed and all resources referred by it can
	 * now be manipulated.
	 *
	 * This is done after a swap so isn't time critical.
	 */
	os_mutex_lock(&r->c->vk.queue_mutex);
	r->c->vk.vkDeviceWaitIdle(r->c->vk.device);
	os_mutex_unlock(&r->c->vk.queue_mutex);
}

static void
renderer_allocate_renderings(struct comp_renderer *r)
{
	if (r->num_buffers == 0) {
		COMP_ERROR(r->c, "Requested 0 command buffers.");
		return;
	}

	COMP_DEBUG(r->c, "Allocating %d Command Buffers.", r->num_buffers);

	if (r->rrs != NULL) {
		free(r->rrs);
	}

	r->rrs = U_TYPED_ARRAY_CALLOC(struct comp_rendering, r->num_buffers);
}

static void
renderer_close_renderings(struct comp_renderer *r)
{
	for (uint32_t i = 0; i < r->num_buffers; i++) {
		comp_rendering_close(&r->rrs[i]);
	}

	free(r->rrs);
	r->rrs = NULL;
}

static void
renderer_init_semaphores(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;
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
renderer_resize(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;

	/*
	 * This makes sure that any pending command buffer has completed
	 * and all resources referred by it can now be manipulated. This
	 * make sure that validation doesn't complain. This is done
	 * during resize so isn't time critical.
	 */
	os_mutex_lock(&vk->queue_mutex);
	vk->vkDeviceWaitIdle(vk->device);
	os_mutex_unlock(&vk->queue_mutex);

	comp_target_create_images(      //
	    r->c->target,               //
	    r->c->target->width,        //
	    r->c->target->height,       //
	    r->settings->color_format,  //
	    r->settings->color_space,   //
	    r->settings->present_mode); //

	renderer_close_renderings(r);

	r->num_buffers = r->c->target->num_images;

	renderer_allocate_renderings(r);
	renderer_build_renderings(r);
}

static void
renderer_acquire_swapchain_image(struct comp_renderer *r)
{
	VkResult ret;

	ret = comp_target_acquire(r->c->target, r->semaphores.present_complete, &r->current_buffer);

	if ((ret == VK_ERROR_OUT_OF_DATE_KHR) || (ret == VK_SUBOPTIMAL_KHR)) {
		COMP_DEBUG(r->c, "Received %s.", vk_result_string(ret));
		renderer_resize(r);

		/* Acquire image again to silence validation error */
		ret = comp_target_acquire(r->c->target, r->semaphores.present_complete, &r->current_buffer);
		if (ret != VK_SUCCESS) {
			COMP_ERROR(r->c, "vk_swapchain_acquire_next_image: %s", vk_result_string(ret));
		}
	} else if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vk_swapchain_acquire_next_image: %s", vk_result_string(ret));
	}
}

static void
renderer_present_swapchain_image(struct comp_renderer *r)
{
	VkResult ret;

	ret = comp_target_present(r->c->target, r->queue, r->current_buffer, r->semaphores.render_complete);
	if (ret == VK_ERROR_OUT_OF_DATE_KHR) {
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
	struct vk_bundle *vk = &r->c->vk;

	// Fences
	for (uint32_t i = 0; i < r->num_buffers; i++)
		vk->vkDestroyFence(vk->device, r->fences[i], NULL);
	free(r->fences);

	// Command buffers
	renderer_close_renderings(r);
	if (r->rrs != NULL) {
		free(r->rrs);
	}

	r->num_buffers = 0;

	// Semaphores
	if (r->semaphores.present_complete != VK_NULL_HANDLE) {
		vk->vkDestroySemaphore(vk->device, r->semaphores.present_complete, NULL);
		r->semaphores.present_complete = VK_NULL_HANDLE;
	}
	if (r->semaphores.render_complete != VK_NULL_HANDLE) {
		vk->vkDestroySemaphore(vk->device, r->semaphores.render_complete, NULL);
		r->semaphores.render_complete = VK_NULL_HANDLE;
	}

	comp_layer_renderer_destroy(r->lr);

	free(r->lr);
}

void
comp_renderer_allocate_layers(struct comp_renderer *self, uint32_t num_layers)
{
	comp_layer_renderer_allocate_layers(self->lr, num_layers);
}

void
comp_renderer_destroy_layers(struct comp_renderer *self)
{
	comp_layer_renderer_destroy_layers(self->lr);
}
