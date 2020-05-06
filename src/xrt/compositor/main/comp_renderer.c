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
	bool one_buffer_imported[2];

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

	struct comp_swapchain_image dummy_images[2];

	struct comp_compositor *c;
	struct comp_settings *settings;
	struct comp_distortion *distortion;
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
renderer_set_swapchain_image(struct comp_renderer *r,
                             uint32_t eye,
                             struct comp_swapchain_image *image,
                             uint32_t layer);

static void
renderer_render(struct comp_renderer *r);

static void
renderer_submit_queue(struct comp_renderer *r);

static void
renderer_build_command_buffers(struct comp_renderer *r);

static void
renderer_rebuild_command_buffers(struct comp_renderer *r);

static void
renderer_build_command_buffer(struct comp_renderer *r,
                              VkCommandBuffer command_buffer,
                              VkFramebuffer framebuffer);

static void
renderer_init_descriptor_pool(struct comp_renderer *r);

static void
renderer_init_dummy_images(struct comp_renderer *r);

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
comp_renderer_frame(struct comp_renderer *r,
                    struct comp_swapchain_image *left,
                    uint32_t left_layer,
                    struct comp_swapchain_image *right,
                    uint32_t right_layer)
{
	renderer_set_swapchain_image(r, 0, left, left_layer);
	renderer_set_swapchain_image(r, 1, right, right_layer);
	renderer_render(r);
}

void
comp_renderer_frame_cached(struct comp_renderer *r)
{
	renderer_render(r);
}

void
comp_renderer_destroy(struct comp_renderer *r)
{
	renderer_destroy(r);
	free(r);
}

void
comp_renderer_reset(struct comp_renderer *r)
{
	r->one_buffer_imported[0] = false;
	r->one_buffer_imported[1] = false;
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

	r->one_buffer_imported[0] = false;
	r->one_buffer_imported[1] = false;
	r->current_buffer = 0;
	r->queue = VK_NULL_HANDLE;
	r->render_pass = VK_NULL_HANDLE;
	r->descriptor_pool = VK_NULL_HANDLE;
	r->pipeline_cache = VK_NULL_HANDLE;
	r->semaphores.present_complete = VK_NULL_HANDLE;
	r->semaphores.render_complete = VK_NULL_HANDLE;

	U_ZERO(&r->dummy_images[0]);
	U_ZERO(&r->dummy_images[1]);
	r->dummy_images[0].views = U_TYPED_CALLOC(VkImageView);
	r->dummy_images[1].views = U_TYPED_CALLOC(VkImageView);

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
renderer_rebuild_command_buffers(struct comp_renderer *r)
{
	renderer_destroy_command_buffers(r);

	r->num_buffers = r->c->window->swapchain.image_count;

	renderer_allocate_command_buffers(r);
	renderer_build_command_buffers(r);
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
_set_image_layout(struct vk_bundle *vk,
                  VkCommandBuffer cmd_buffer,
                  VkImage image,
                  VkAccessFlags src_access_mask,
                  VkAccessFlags dst_access_mask,
                  VkImageLayout old_layout,
                  VkImageLayout new_layout,
                  VkImageSubresourceRange subresource_range)
{
	VkImageMemoryBarrier barrier = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	    .srcAccessMask = src_access_mask,
	    .dstAccessMask = dst_access_mask,
	    .oldLayout = old_layout,
	    .newLayout = new_layout,
	    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .image = image,
	    .subresourceRange = subresource_range,
	};

	vk->vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
	                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL,
	                         0, NULL, 1, &barrier);
}

static void
renderer_init_dummy_images(struct comp_renderer *r)
{
	struct vk_bundle *vk = &r->c->vk;
	VkCommandBuffer cmd_buffer;
	if (vk_init_cmd_buffer(vk, &cmd_buffer) != VK_SUCCESS)
		return;

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = 1};

	VkClearColorValue color = {.float32 = {0.3, 0.3, 0.3, 1}};
	VkExtent2D extent = {.width = 640, .height = 800};

	VkImageUsageFlags usage =
	    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	for (uint32_t i = 0; i < 2; i++) {
		vk_create_image_simple(
		    &r->c->vk, extent, VK_FORMAT_B8G8R8A8_SRGB, usage,
		    &r->dummy_images[i].memory, &r->dummy_images[i].image);

		_set_image_layout(
		    vk, cmd_buffer, r->dummy_images[i].image, 0,
		    VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
		    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresource_range);

		vk->vkCmdClearColorImage(cmd_buffer, r->dummy_images[i].image,
		                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                         &color, 1, &subresource_range);

		vk_set_image_layout(vk, cmd_buffer, r->dummy_images[i].image,
		                    VK_ACCESS_TRANSFER_WRITE_BIT,
		                    VK_ACCESS_SHADER_READ_BIT,
		                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                    subresource_range);

		vk_create_sampler(vk, &r->dummy_images[i].sampler);
		vk_create_view(vk, r->dummy_images[i].image,
		               VK_FORMAT_B8G8R8A8_SRGB, subresource_range,
		               &r->dummy_images[i].views[0]);
	}

	vk_submit_cmd_buffer(vk, cmd_buffer);
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
_set_dummy_images(struct comp_renderer *r)
{
	for (uint32_t i = 0; i < 2; i++)
		comp_distortion_update_descriptor_set(
		    r->distortion, r->dummy_images[i].sampler,
		    r->dummy_images[i].views[0], i);
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

	renderer_init_dummy_images(r);

	renderer_init_descriptor_pool(r);

	r->distortion = U_TYPED_CALLOC(struct comp_distortion);

	comp_distortion_init(r->distortion, r->c, r->render_pass,
	                     r->pipeline_cache, r->settings->distortion_model,
	                     r->c->xdev->hmd, r->descriptor_pool,
	                     r->settings->flip_y);

	_set_dummy_images(r);

	renderer_build_command_buffers(r);
}

void
comp_renderer_set_idle_images(struct comp_renderer *r)
{
	_set_dummy_images(r);
	renderer_rebuild_command_buffers(r);
}

static void
renderer_set_swapchain_image(struct comp_renderer *r,
                             uint32_t eye,
                             struct comp_swapchain_image *image,
                             uint32_t layer)
{
	if (eye > 1) {
		COMP_ERROR(r->c, "Swapchain image %p %u not found",
		           (void *)image, eye);
		return;
	}

	if (!r->one_buffer_imported[eye]) {
		COMP_DEBUG(r->c,
		           "Updating descriptor set for"
		           " swapchain image %p and eye %u",
		           (void *)image, eye);
		comp_distortion_update_descriptor_set(
		    r->distortion, image->sampler, image->views[layer],
		    (uint32_t)eye);
		renderer_rebuild_command_buffers(r);
		r->one_buffer_imported[eye] = true;
	}
}

static void
renderer_render(struct comp_renderer *r)
{
	r->c->window->flush(r->c->window);
	renderer_acquire_swapchain_image(r);
	renderer_submit_queue(r);
	renderer_present_swapchain_image(r);
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

	// Dummy images
	for (uint32_t i = 0; i < 2; i++) {
		comp_swapchain_image_cleanup(vk, 1, &r->dummy_images[i]);
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
}
