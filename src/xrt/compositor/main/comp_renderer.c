// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor rendering code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "util/u_misc.h"

#include "xrt/xrt_compositor.h"
#include "main/comp_distortion.h"
#include "main/comp_layer_renderer.h"
#include "math/m_api.h"

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
	VkRenderPass render_pass;
	VkDescriptorPool descriptor_pool;
	VkPipelineCache pipeline_cache;

	struct
	{
		VkSemaphore present_complete;
		VkSemaphore render_complete;
	} semaphores;

	VkCommandBuffer *cmd_buffers;
	VkFramebuffer *frame_buffers;
	VkFence *fences;
	uint32_t num_buffers;

	struct comp_compositor *c;
	struct comp_settings *settings;
	struct comp_distortion *distortion;

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
renderer_build_command_buffers(struct comp_renderer *r);

static void
renderer_build_command_buffer(struct comp_renderer *r,
                              VkCommandBuffer command_buffer,
                              VkFramebuffer framebuffer);

static void
renderer_init_descriptor_pool(struct comp_renderer *r);

static void
renderer_create_frame_buffer(struct comp_renderer *r,
                             VkFramebuffer *frame_buffer,
                             uint32_t num_attachements,
                             VkImageView *attachments);

static void
renderer_allocate_command_buffers(struct comp_renderer *r);

static void
renderer_destroy_command_buffers(struct comp_renderer *r);

static void
renderer_create_pipeline_cache(struct comp_renderer *r);

static void
renderer_init_semaphores(struct comp_renderer *r);

static void
renderer_resize(struct comp_renderer *r);

static void
renderer_create_frame_buffers(struct comp_renderer *r);

static void
renderer_create_render_pass(struct comp_renderer *r);

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
	r->render_pass = VK_NULL_HANDLE;
	r->descriptor_pool = VK_NULL_HANDLE;
	r->pipeline_cache = VK_NULL_HANDLE;
	r->semaphores.present_complete = VK_NULL_HANDLE;
	r->semaphores.render_complete = VK_NULL_HANDLE;

	r->distortion = NULL;
	r->cmd_buffers = NULL;
	r->frame_buffers = NULL;
}

static void
renderer_submit_queue(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;
	VkResult ret;

	VkPipelineStageFlags stage_flags[1] = {
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	};

	ret = vk->vkWaitForFences(vk->device, 1, &r->fences[r->current_buffer],
	                          VK_TRUE, UINT64_MAX);
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
	    .pCommandBuffers = &r->cmd_buffers[r->current_buffer],
	    .signalSemaphoreCount = 1,
	    .pSignalSemaphores = &r->semaphores.render_complete,
	};

	ret = vk->vkQueueSubmit(r->queue, 1, &comp_submit_info,
	                        r->fences[r->current_buffer]);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkQueueSubmit: %s", vk_result_string(ret));
	}
}

static void
renderer_build_command_buffers(struct comp_renderer *r)
{
	for (uint32_t i = 0; i < r->num_buffers; ++i)
		renderer_build_command_buffer(r, r->cmd_buffers[i],
		                              r->frame_buffers[i]);
}

static void
renderer_set_viewport_scissor(float scale_x,
                              float scale_y,
                              VkViewport *v,
                              VkRect2D *s,
                              struct xrt_view *view)
{
	v->x = view->viewport.x_pixels * scale_x;
	v->y = view->viewport.y_pixels * scale_y;
	v->width = view->viewport.w_pixels * scale_x;
	v->height = view->viewport.h_pixels * scale_y;

	s->offset.x = (int32_t)(view->viewport.x_pixels * scale_x);
	s->offset.y = (int32_t)(view->viewport.y_pixels * scale_y);
	s->extent.width = (uint32_t)(view->viewport.w_pixels * scale_x);
	s->extent.height = (uint32_t)(view->viewport.h_pixels * scale_y);
}

static void
renderer_build_command_buffer(struct comp_renderer *r,
                              VkCommandBuffer command_buffer,
                              VkFramebuffer framebuffer)
{
	struct vk_bundle *vk = &r->c->vk;
	VkResult ret;

	VkClearValue clear_color = {
	    .color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}};

	VkCommandBufferBeginInfo command_buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	ret = vk->vkBeginCommandBuffer(command_buffer, &command_buffer_info);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkBeginCommandBuffer: %s",
		           vk_result_string(ret));
		return;
	}

	VkRenderPassBeginInfo render_pass_begin_info = {
	    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
	    .renderPass = r->render_pass,
	    .framebuffer = framebuffer,
	    .renderArea =
	        {
	            .offset =
	                {
	                    .x = 0,
	                    .y = 0,
	                },
	            .extent =
	                {
	                    .width = r->c->current.width,
	                    .height = r->c->current.height,
	                },
	        },
	    .clearValueCount = 1,
	    .pClearValues = &clear_color,
	};
	vk->vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info,
	                         VK_SUBPASS_CONTENTS_INLINE);


	// clang-format off
	float scale_x = (float)r->c->current.width /
	                (float)r->c->xdev->hmd->screens[0].w_pixels;
	float scale_y = (float)r->c->current.height /
	                (float)r->c->xdev->hmd->screens[0].h_pixels;
	// clang-format on

	VkViewport viewport = {
	    .x = 0,
	    .y = 0,
	    .width = 0,
	    .height = 0,
	    .minDepth = 0.0f,
	    .maxDepth = 1.0f,
	};

	VkRect2D scissor = {
	    .offset = {.x = 0, .y = 0},
	    .extent = {.width = 0, .height = 0},
	};

	renderer_set_viewport_scissor(scale_x, scale_y, &viewport, &scissor,
	                              &r->c->xdev->hmd->views[0]);
	vk->vkCmdSetViewport(command_buffer, 0, 1, &viewport);
	vk->vkCmdSetScissor(command_buffer, 0, 1, &scissor);

	if (r->distortion->distortion_model == XRT_DISTORTION_MODEL_MESHUV) {
		// Mesh distortion
		comp_distortion_draw_mesh(r->distortion, command_buffer, 0);
		renderer_set_viewport_scissor(scale_x, scale_y, &viewport,
		                              &scissor,
		                              &r->c->xdev->hmd->views[1]);
		vk->vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vk->vkCmdSetScissor(command_buffer, 0, 1, &scissor);
		comp_distortion_draw_mesh(r->distortion, command_buffer, 1);

	} else {
		// Fragment shader distortion
		comp_distortion_draw_quad(r->distortion, command_buffer, 0);
		renderer_set_viewport_scissor(scale_x, scale_y, &viewport,
		                              &scissor,
		                              &r->c->xdev->hmd->views[1]);
		vk->vkCmdSetViewport(command_buffer, 0, 1, &viewport);
		vk->vkCmdSetScissor(command_buffer, 0, 1, &scissor);
		comp_distortion_draw_quad(r->distortion, command_buffer, 1);
	}

	vk->vkCmdEndRenderPass(command_buffer);

	ret = vk->vkEndCommandBuffer(command_buffer);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkEndCommandBuffer: %s",
		           vk_result_string(ret));
		return;
	}
}

static void
renderer_init_descriptor_pool(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;
	VkResult ret;

	VkDescriptorPoolSize pool_sizes[2] = {
	    {
	        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	        .descriptorCount = 4,
	    },
	    {
	        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .descriptorCount = 2,
	    },
	};

	VkDescriptorPoolCreateInfo descriptor_pool_info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	    .maxSets = 2,
	    .poolSizeCount = ARRAY_SIZE(pool_sizes),
	    .pPoolSizes = pool_sizes,
	};

	ret = vk->vkCreateDescriptorPool(vk->device, &descriptor_pool_info,
	                                 NULL, &r->descriptor_pool);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkCreateDescriptorPool: %s",
		           vk_result_string(ret));
	}
}

static void
_create_fences(struct comp_renderer *r)
{
	r->fences = U_TYPED_ARRAY_CALLOC(VkFence, r->num_buffers);

	struct vk_bundle *vk = &r->c->vk;

	for (uint32_t i = 0; i < r->num_buffers; i++) {
		VkResult ret = vk->vkCreateFence(
		    vk->device,
		    &(VkFenceCreateInfo){
		        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		        .flags = VK_FENCE_CREATE_SIGNALED_BIT},
		    NULL, &r->fences[i]);
		if (ret != VK_SUCCESS) {
			COMP_ERROR(r->c, "vkCreateFence: %s",
			           vk_result_string(ret));
		}
	}
}

static void
_get_view_projection(struct comp_renderer *r)
{
	struct xrt_space_relation relation;
	uint64_t out_timestamp;

	xrt_device_get_tracked_pose(r->c->xdev, XRT_INPUT_GENERIC_HEAD_POSE,
	                            r->c->last_frame_time_ns, &out_timestamp,
	                            &relation);

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
		xrt_device_get_view_pose(r->c->xdev, &eye_relation, i,
		                         &eye_pose);

		struct xrt_pose world_pose;
		math_pose_openxr_locate(&eye_pose, &relation.pose,
		                        &base_space_pose, &world_pose);

		comp_layer_renderer_set_pose(r->lr, &eye_pose, &world_pose, i);
	}
}

static void
renderer_init(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;

	vk->vkGetDeviceQueue(vk->device, r->c->vk.queue_family_index, 0,
	                     &r->queue);
	renderer_init_semaphores(r);
	renderer_create_pipeline_cache(r);
	renderer_create_render_pass(r);

	assert(r->c->window->swapchain.image_count > 0);

	r->num_buffers = r->c->window->swapchain.image_count;

	_create_fences(r);
	renderer_create_frame_buffers(r);
	renderer_allocate_command_buffers(r);

	renderer_init_descriptor_pool(r);

	r->distortion = U_TYPED_CALLOC(struct comp_distortion);

	comp_distortion_init(r->distortion, r->c, r->render_pass,
	                     r->pipeline_cache, r->settings->distortion_model,
	                     r->c->xdev->hmd, r->descriptor_pool,
	                     r->settings->flip_y);

	VkExtent2D extent = {
	    .width = r->c->xdev->hmd->screens[0].w_pixels,
	    .height = r->c->xdev->hmd->screens[0].h_pixels,
	};

	r->lr = comp_layer_renderer_create(vk, extent, VK_FORMAT_B8G8R8A8_SRGB);

	for (uint32_t i = 0; i < 2; i++) {
		comp_distortion_update_descriptor_set(
		    r->distortion, r->lr->framebuffers[i].sampler,
		    r->lr->framebuffers[i].view, i, false);
	}

	renderer_build_command_buffers(r);
}

VkImageView
get_image_view(struct comp_swapchain_image *image,
               enum xrt_layer_composition_flags flags,
               uint32_t array_index)
{
	if (flags & XRT_LAYER_COMPOSITION_BLEND_TEXTURE_SOURCE_ALPHA_BIT) {
		return image->views.alpha[array_index];
	} else {
		return image->views.no_alpha[array_index];
	}
}
void
comp_renderer_set_quad_layer(struct comp_renderer *r,
                             uint32_t layer,
                             struct comp_swapchain_image *image,
                             struct xrt_layer_data *data)
{
	comp_layer_update_descriptors(
	    r->lr->layers[layer], image->sampler,
	    get_image_view(image, data->flags, data->quad.sub.array_index));

	struct xrt_matrix_4x4 model_matrix;
	math_matrix_4x4_quad_model(&data->quad.pose, &data->quad.size,
	                           &model_matrix);

	comp_layer_set_model_matrix(r->lr->layers[layer], &model_matrix);

	comp_layer_set_flip_y(r->lr->layers[layer], data->flip_y);

	r->lr->layers[layer]->type = XRT_LAYER_QUAD;
	r->lr->layers[layer]->visibility = data->quad.visibility;
	r->lr->layers[layer]->flags = data->flags;
	r->lr->layers[layer]->view_space =
	    (data->flags & XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT) != 0;

	r->c->vk.vkDeviceWaitIdle(r->c->vk.device);
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

	comp_layer_update_stereo_descriptors(
	    r->lr->layers[layer], left_image->sampler, right_image->sampler,
	    get_image_view(left_image, data->flags, left_array_index),
	    get_image_view(right_image, data->flags, right_array_index));

	comp_layer_set_flip_y(r->lr->layers[layer], data->flip_y);

	r->lr->layers[layer]->type = XRT_LAYER_STEREO_PROJECTION;
	r->lr->layers[layer]->view_space =
	    (data->flags & XRT_LAYER_COMPOSITION_VIEW_SPACE_BIT) != 0;
}

void
comp_renderer_draw(struct comp_renderer *r)
{
	_get_view_projection(r);
	comp_layer_renderer_draw(r->lr);
	r->c->vk.vkDeviceWaitIdle(r->c->vk.device);

	r->c->window->flush(r->c->window);
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
	r->c->vk.vkDeviceWaitIdle(r->c->vk.device);
}

static void
renderer_create_frame_buffer(struct comp_renderer *r,
                             VkFramebuffer *frame_buffer,
                             uint32_t num_attachements,
                             VkImageView *attachments)
{
	struct vk_bundle *vk = &r->c->vk;
	VkResult ret;

	VkFramebufferCreateInfo frame_buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
	    .renderPass = r->render_pass,
	    .attachmentCount = num_attachements,
	    .pAttachments = attachments,
	    .width = r->c->current.width,
	    .height = r->c->current.height,
	    .layers = 1,
	};

	ret = vk->vkCreateFramebuffer(vk->device, &frame_buffer_info, NULL,
	                              frame_buffer);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkCreateFramebuffer: %s",
		           vk_result_string(ret));
	}
}

static void
renderer_allocate_command_buffers(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;
	VkResult ret;

	if (r->num_buffers == 0) {
		COMP_ERROR(r->c, "Requested 0 command buffers.");
		return;
	}

	COMP_DEBUG(r->c, "Allocating %d Command Buffers.", r->num_buffers);

	if (r->cmd_buffers != NULL)
		free(r->cmd_buffers);

	r->cmd_buffers = U_TYPED_ARRAY_CALLOC(VkCommandBuffer, r->num_buffers);

	VkCommandBufferAllocateInfo cmd_buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	    .commandPool = vk->cmd_pool,
	    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	    .commandBufferCount = r->num_buffers,
	};

	ret = vk->vkAllocateCommandBuffers(vk->device, &cmd_buffer_info,
	                                   r->cmd_buffers);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkCreateFramebuffer: %s",
		           vk_result_string(ret));
	}
}

static void
renderer_destroy_command_buffers(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;

	vk->vkFreeCommandBuffers(vk->device, vk->cmd_pool, r->num_buffers,
	                         r->cmd_buffers);
}

static void
renderer_create_pipeline_cache(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;
	VkResult ret;

	VkPipelineCacheCreateInfo pipeline_cache_info = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
	};
	ret = vk->vkCreatePipelineCache(vk->device, &pipeline_cache_info, NULL,
	                                &r->pipeline_cache);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkCreatePipelineCache: %s",
		           vk_result_string(ret));
	}
}

static void
renderer_init_semaphores(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;
	VkResult ret;

	VkSemaphoreCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	ret = vk->vkCreateSemaphore(vk->device, &info, NULL,
	                            &r->semaphores.present_complete);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkCreateSemaphore: %s",
		           vk_result_string(ret));
	}

	ret = vk->vkCreateSemaphore(vk->device, &info, NULL,
	                            &r->semaphores.render_complete);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkCreateSemaphore: %s",
		           vk_result_string(ret));
	}
}

static void
renderer_resize(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;

	vk->vkDeviceWaitIdle(vk->device);

	vk_swapchain_create(&r->c->window->swapchain, r->c->current.width,
	                    r->c->current.height, r->settings->color_format,
	                    r->settings->color_space,
	                    r->settings->present_mode);

	for (uint32_t i = 0; i < r->num_buffers; i++)
		vk->vkDestroyFramebuffer(vk->device, r->frame_buffers[i], NULL);
	renderer_destroy_command_buffers(r);

	r->num_buffers = r->c->window->swapchain.image_count;

	renderer_create_frame_buffers(r);
	renderer_allocate_command_buffers(r);
	renderer_build_command_buffers(r);
}

static void
renderer_create_frame_buffers(struct comp_renderer *r)
{
	if (r->frame_buffers != NULL)
		free(r->frame_buffers);

	r->frame_buffers = U_TYPED_ARRAY_CALLOC(VkFramebuffer, r->num_buffers);

	for (uint32_t i = 0; i < r->num_buffers; i++) {
		VkImageView attachments[1] = {
		    r->c->window->swapchain.buffers[i].view,
		};
		renderer_create_frame_buffer(r, &r->frame_buffers[i],
		                             ARRAY_SIZE(attachments),
		                             attachments);
	}
}

static void
renderer_create_render_pass(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;
	VkResult ret;

	VkAttachmentDescription attachments[1] = {
	    (VkAttachmentDescription){
	        .format = r->c->window->swapchain.surface_format.format,
	        .samples = VK_SAMPLE_COUNT_1_BIT,
	        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
	        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	    },
	};

	VkAttachmentReference color_reference = {
	    .attachment = 0,
	    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	VkSubpassDescription subpass_description = {
	    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
	    .inputAttachmentCount = 0,
	    .pInputAttachments = NULL,
	    .colorAttachmentCount = 1,
	    .pColorAttachments = &color_reference,
	    .pResolveAttachments = NULL,
	    .pDepthStencilAttachment = NULL,
	    .preserveAttachmentCount = 0,
	    .pPreserveAttachments = NULL,
	};

	VkSubpassDependency dependencies[1] = {
	    (VkSubpassDependency){
	        .srcSubpass = VK_SUBPASS_EXTERNAL,
	        .dstSubpass = 0,
	        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	        .srcAccessMask = 0,
	        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
	                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	    },
	};

	VkRenderPassCreateInfo render_pass_info = {
	    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
	    .attachmentCount = ARRAY_SIZE(attachments),
	    .pAttachments = attachments,
	    .subpassCount = 1,
	    .pSubpasses = &subpass_description,
	    .dependencyCount = ARRAY_SIZE(dependencies),
	    .pDependencies = dependencies,
	};

	ret = vk->vkCreateRenderPass(vk->device, &render_pass_info, NULL,
	                             &r->render_pass);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vkCreateRenderPass: %s",
		           vk_result_string(ret));
	}
}

static void
renderer_acquire_swapchain_image(struct comp_renderer *r)
{
	VkResult ret;

	ret = vk_swapchain_acquire_next_image(&r->c->window->swapchain,
	                                      r->semaphores.present_complete,
	                                      &r->current_buffer);

	if ((ret == VK_ERROR_OUT_OF_DATE_KHR) || (ret == VK_SUBOPTIMAL_KHR)) {
		COMP_DEBUG(r->c, "Received %s.", vk_result_string(ret));
		renderer_resize(r);
		/* Acquire image again to silence validation error */
		ret = vk_swapchain_acquire_next_image(
		    &r->c->window->swapchain, r->semaphores.present_complete,
		    &r->current_buffer);
		if (ret != VK_SUCCESS) {
			COMP_ERROR(r->c, "vk_swapchain_acquire_next_image: %s",
			           vk_result_string(ret));
		}
	} else if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vk_swapchain_acquire_next_image: %s",
		           vk_result_string(ret));
	}
}

static void
renderer_present_swapchain_image(struct comp_renderer *r)
{
	VkResult ret;

	ret = vk_swapchain_present(&r->c->window->swapchain, r->queue,
	                           r->current_buffer,
	                           r->semaphores.render_complete);
	if (ret == VK_ERROR_OUT_OF_DATE_KHR) {
		renderer_resize(r);
		return;
	}
	if (ret != VK_SUCCESS) {
		COMP_ERROR(r->c, "vk_swapchain_present: %s",
		           vk_result_string(ret));
	}
}

static void
renderer_destroy(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;

	// Distortion
	if (r->distortion != NULL) {
		comp_distortion_destroy(r->distortion);
		r->distortion = NULL;
	}

	// Discriptor pool
	if (r->descriptor_pool != VK_NULL_HANDLE) {
		vk->vkDestroyDescriptorPool(vk->device, r->descriptor_pool,
		                            NULL);
		r->descriptor_pool = VK_NULL_HANDLE;
	}

	// Fences
	for (uint32_t i = 0; i < r->num_buffers; i++)
		vk->vkDestroyFence(vk->device, r->fences[i], NULL);
	free(r->fences);

	// Command buffers
	renderer_destroy_command_buffers(r);
	if (r->cmd_buffers != NULL)
		free(r->cmd_buffers);

	// Render pass
	if (r->render_pass != VK_NULL_HANDLE) {
		vk->vkDestroyRenderPass(vk->device, r->render_pass, NULL);
		r->render_pass = VK_NULL_HANDLE;
	}

	// Frame buffers
	for (uint32_t i = 0; i < r->num_buffers; i++) {
		if (r->frame_buffers[i] != VK_NULL_HANDLE) {
			vk->vkDestroyFramebuffer(vk->device,
			                         r->frame_buffers[i], NULL);
			r->frame_buffers[i] = VK_NULL_HANDLE;
		}
	}
	if (r->frame_buffers != NULL)
		free(r->frame_buffers);
	r->frame_buffers = NULL;
	r->num_buffers = 0;

	// Pipeline cache
	if (r->pipeline_cache != VK_NULL_HANDLE) {
		vk->vkDestroyPipelineCache(vk->device, r->pipeline_cache, NULL);
		r->pipeline_cache = VK_NULL_HANDLE;
	}

	// Semaphores
	if (r->semaphores.present_complete != VK_NULL_HANDLE) {
		vk->vkDestroySemaphore(vk->device,
		                       r->semaphores.present_complete, NULL);
		r->semaphores.present_complete = VK_NULL_HANDLE;
	}
	if (r->semaphores.render_complete != VK_NULL_HANDLE) {
		vk->vkDestroySemaphore(vk->device,
		                       r->semaphores.render_complete, NULL);
		r->semaphores.render_complete = VK_NULL_HANDLE;
	}

	comp_layer_renderer_destroy(r->lr);
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
