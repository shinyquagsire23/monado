// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor quad rendering.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_main
 */

#include "util/u_misc.h"
#include "math/m_api.h"

#include "comp_layer_renderer.h"

#include <stdio.h>
#include <math.h>


struct comp_layer_vertex
{
	float position[3];
	float uv[2];
};

static const VkClearColorValue background_color = {
    .float32 = {0.3f, 0.3f, 0.3f, 1.0f},
};

static bool
_init_render_pass(struct vk_bundle *vk,
                  VkFormat format,
                  VkImageLayout final_layout,
                  VkSampleCountFlagBits sample_count,
                  VkRenderPass *out_render_pass)
{
	VkAttachmentDescription *attachments = (VkAttachmentDescription[]){
	    {
	        .format = format,
	        .samples = sample_count,
	        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
	        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	        .finalLayout = final_layout,
	        .flags = 0,
	    },
	};

	VkRenderPassCreateInfo renderpass_info = {
	    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
	    .flags = 0,
	    .attachmentCount = 1,
	    .pAttachments = attachments,
	    .subpassCount = 1,
	    .pSubpasses =
	        &(VkSubpassDescription){
	            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
	            .colorAttachmentCount = 1,
	            .pColorAttachments =
	                &(VkAttachmentReference){
	                    .attachment = 0,
	                    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                },
	            .pDepthStencilAttachment = NULL,
	            .pResolveAttachments = NULL,
	        },
	    .dependencyCount = 0,
	    .pDependencies = NULL,
	};

	VkResult res = vk->vkCreateRenderPass(vk->device, &renderpass_info, NULL, out_render_pass);
	vk_check_error("vkCreateRenderPass", res, false);

	return true;
}

static bool
_init_descriptor_layout(struct comp_layer_renderer *self)
{
	struct vk_bundle *vk = self->vk;

	VkDescriptorSetLayoutCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	    .bindingCount = 2,
	    .pBindings =
	        (VkDescriptorSetLayoutBinding[]){
	            {
	                .binding = self->transformation_ubo_binding,
	                .descriptorCount = 1,
	                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	            },
	            {
	                .binding = self->texture_binding,
	                .descriptorCount = 1,
	                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	            },
	        },
	};

	VkResult res = vk->vkCreateDescriptorSetLayout(vk->device, &info, NULL, &self->descriptor_set_layout);

	vk_check_error("vkCreateDescriptorSetLayout", res, false);

	return true;
}

static bool
_init_descriptor_layout_equirect(struct comp_layer_renderer *self)
{
	struct vk_bundle *vk = self->vk;

	VkDescriptorSetLayoutCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	    .bindingCount = 1,
	    .pBindings =
	        (VkDescriptorSetLayoutBinding[]){
	            {
	                .binding = 0,
	                .descriptorCount = 1,
	                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	            },
	        },
	};

	VkResult res = vk->vkCreateDescriptorSetLayout(vk->device, &info, NULL, &self->descriptor_set_layout_equirect);

	vk_check_error("vkCreateDescriptorSetLayout", res, false);

	return true;
}

static bool
_init_pipeline_layout(struct comp_layer_renderer *self)
{
	struct vk_bundle *vk = self->vk;

	const VkDescriptorSetLayout set_layouts[2] = {self->descriptor_set_layout,
	                                              self->descriptor_set_layout_equirect};

	VkPipelineLayoutCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	    .setLayoutCount = 2,
	    .pSetLayouts = set_layouts,
	};

	VkResult res = vk->vkCreatePipelineLayout(vk->device, &info, NULL, &self->pipeline_layout);

	vk_check_error("vkCreatePipelineLayout", res, false);

	return true;
}

static bool
_init_pipeline_cache(struct comp_layer_renderer *self)
{
	struct vk_bundle *vk = self->vk;

	VkPipelineCacheCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
	};

	VkResult res = vk->vkCreatePipelineCache(vk->device, &info, NULL, &self->pipeline_cache);

	vk_check_error("vkCreatePipelineCache", res, false);

	return true;
}

// These are MSVC-style pragmas, but supported by GCC since early in the 4
// series.
#pragma pack(push, 1)
struct comp_pipeline_config
{
	VkPrimitiveTopology topology;
	uint32_t stride;
	const VkVertexInputAttributeDescription *attribs;
	uint32_t attrib_count;
	const VkPipelineDepthStencilStateCreateInfo *depth_stencil_state;
	const VkPipelineColorBlendAttachmentState *blend_attachments;
	const VkPipelineRasterizationStateCreateInfo *rasterization_state;
};
#pragma pack(pop)

static bool
_init_graphics_pipeline(struct comp_layer_renderer *self,
                        VkShaderModule shader_vert,
                        VkShaderModule shader_frag,
                        bool premultiplied_alpha,
                        VkPipeline *pipeline)
{
	struct vk_bundle *vk = self->vk;

	VkBlendFactor blend_factor = premultiplied_alpha ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;

	struct comp_pipeline_config config = {
	    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	    .stride = sizeof(struct comp_layer_vertex),
	    .attribs =
	        (VkVertexInputAttributeDescription[]){
	            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
	            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(struct comp_layer_vertex, uv)},
	        },
	    .attrib_count = 2,
	    .depth_stencil_state =
	        &(VkPipelineDepthStencilStateCreateInfo){
	            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
	            .depthTestEnable = VK_FALSE,
	            .depthWriteEnable = VK_FALSE,
	            .depthCompareOp = VK_COMPARE_OP_NEVER,
	        },
	    .blend_attachments =
	        &(VkPipelineColorBlendAttachmentState){
	            .blendEnable = VK_TRUE,
	            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
	                              VK_COLOR_COMPONENT_A_BIT,
	            .srcColorBlendFactor = blend_factor,
	            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	            .colorBlendOp = VK_BLEND_OP_ADD,
	            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
	            .alphaBlendOp = VK_BLEND_OP_ADD,
	        },
	    .rasterization_state =
	        &(VkPipelineRasterizationStateCreateInfo){
	            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
	            .polygonMode = VK_POLYGON_MODE_FILL,
	            .cullMode = VK_CULL_MODE_BACK_BIT,
	            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
	            .lineWidth = 1.0f,
	        },
	};

	VkPipelineShaderStageCreateInfo shader_stages[2] = {
	    {
	        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
	        .stage = VK_SHADER_STAGE_VERTEX_BIT,
	        .module = shader_vert,
	        .pName = "main",
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
	        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
	        .module = shader_frag,
	        .pName = "main",
	    },
	};

	VkGraphicsPipelineCreateInfo pipeline_info = {
	    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
	    .layout = self->pipeline_layout,
	    .pVertexInputState =
	        &(VkPipelineVertexInputStateCreateInfo){
	            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	            .pVertexAttributeDescriptions = config.attribs,
	            .vertexBindingDescriptionCount = 1,
	            .pVertexBindingDescriptions =
	                &(VkVertexInputBindingDescription){
	                    .binding = 0,
	                    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	                    .stride = config.stride,
	                },
	            .vertexAttributeDescriptionCount = config.attrib_count,
	        },
	    .pInputAssemblyState =
	        &(VkPipelineInputAssemblyStateCreateInfo){
	            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
	            .topology = config.topology,
	            .primitiveRestartEnable = VK_FALSE,
	        },
	    .pViewportState =
	        &(VkPipelineViewportStateCreateInfo){
	            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
	            .viewportCount = 1,
	            .scissorCount = 1,
	        },
	    .pRasterizationState = config.rasterization_state,
	    .pMultisampleState =
	        &(VkPipelineMultisampleStateCreateInfo){
	            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
	            .rasterizationSamples = self->sample_count,
	            .minSampleShading = 0.0f,
	            .pSampleMask = &(uint32_t){0xFFFFFFFF},
	            .alphaToCoverageEnable = VK_FALSE,
	        },
	    .pDepthStencilState = config.depth_stencil_state,
	    .pColorBlendState =
	        &(VkPipelineColorBlendStateCreateInfo){
	            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
	            .logicOpEnable = VK_FALSE,
	            .attachmentCount = 1,
	            .blendConstants = {0, 0, 0, 0},
	            .pAttachments = config.blend_attachments,
	        },
	    .stageCount = 2,
	    .pStages = shader_stages,
	    .renderPass = self->render_pass,
	    .pDynamicState =
	        &(VkPipelineDynamicStateCreateInfo){
	            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
	            .dynamicStateCount = 2,
	            .pDynamicStates =
	                (VkDynamicState[]){
	                    VK_DYNAMIC_STATE_VIEWPORT,
	                    VK_DYNAMIC_STATE_SCISSOR,
	                },
	        },
	    .subpass = VK_NULL_HANDLE,
	};

	VkResult res;
	res = vk->vkCreateGraphicsPipelines(vk->device, self->pipeline_cache, 1, &pipeline_info, NULL, pipeline);

	vk_check_error("vkCreateGraphicsPipelines", res, false);

	return true;
}

// clang-format off
#define PLANE_VERTICES 6
static float plane_vertices[PLANE_VERTICES * 5] = {
	-0.5, -0.5, 0, 0, 1,
	 0.5, -0.5, 0, 1, 1,
	 0.5,  0.5, 0, 1, 0,
	 0.5,  0.5, 0, 1, 0,
	-0.5,  0.5, 0, 0, 0,
	-0.5, -0.5, 0, 0, 1,
};
// clang-format on

static bool
_init_vertex_buffer(struct comp_layer_renderer *self)
{
	struct vk_bundle *vk = self->vk;

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (!vk_buffer_init(vk, sizeof(float) * ARRAY_SIZE(plane_vertices), usage, properties,
	                    &self->vertex_buffer.handle, &self->vertex_buffer.memory))
		return false;

	self->vertex_buffer.size = PLANE_VERTICES;

	return vk_update_buffer(vk, plane_vertices, sizeof(float) * ARRAY_SIZE(plane_vertices),
	                        self->vertex_buffer.memory);
}

static void
_render_eye(struct comp_layer_renderer *self,
            uint32_t eye,
            VkCommandBuffer cmd_buffer,
            VkPipelineLayout pipeline_layout)
{
	struct xrt_matrix_4x4 vp_world;
	struct xrt_matrix_4x4 vp_eye;
	struct xrt_matrix_4x4 vp_inv;
	math_matrix_4x4_multiply(&self->mat_projection[eye], &self->mat_world_view[eye], &vp_world);
	math_matrix_4x4_multiply(&self->mat_projection[eye], &self->mat_eye_view[eye], &vp_eye);

	math_matrix_4x4_inverse_view_projection(&self->mat_world_view[eye], &self->mat_projection[eye], &vp_inv);

	for (uint32_t i = 0; i < self->num_layers; i++) {
		bool unpremultiplied_alpha = self->layers[i]->flags & XRT_LAYER_COMPOSITION_UNPREMULTIPLIED_ALPHA_BIT;

		struct vk_buffer *vertex_buffer;
		if (self->layers[i]->type == XRT_LAYER_CYLINDER) {
			vertex_buffer = comp_layer_get_cylinder_vertex_buffer(self->layers[i]);
		} else {
			vertex_buffer = &self->vertex_buffer;
		}

		VkPipeline pipeline =
		    unpremultiplied_alpha ? self->pipeline_premultiplied_alpha : self->pipeline_unpremultiplied_alpha;

		if (self->layers[i]->type == XRT_LAYER_EQUIRECT2) {
			pipeline = self->pipeline_equirect2;
			comp_layer_draw(self->layers[i], eye, pipeline, pipeline_layout, cmd_buffer, vertex_buffer,
			                &vp_inv, &vp_inv);
		} else if (self->layers[i]->type == XRT_LAYER_EQUIRECT1) {
			pipeline = self->pipeline_equirect1;
			comp_layer_draw(self->layers[i], eye, pipeline, pipeline_layout, cmd_buffer, vertex_buffer,
			                &vp_inv, &vp_inv);
		} else {
			comp_layer_draw(self->layers[i], eye, pipeline, pipeline_layout, cmd_buffer, vertex_buffer,
			                &vp_world, &vp_eye);
		}
	}
}

static bool
_init_frame_buffer(struct comp_layer_renderer *self, VkFormat format, VkRenderPass rp, uint32_t eye)
{
	struct vk_bundle *vk = self->vk;

	VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	VkResult res = vk_create_image_simple(vk, self->extent, format, usage, &self->framebuffers[eye].memory,
	                                      &self->framebuffers[eye].image);
	vk_check_error("vk_create_image_simple", res, false);

	vk_create_sampler(vk, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, &self->framebuffers[eye].sampler);

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = 1,
	};

	res =
	    vk_create_view(vk, self->framebuffers[eye].image, format, subresource_range, &self->framebuffers[eye].view);

	vk_check_error("vk_create_view", res, false);

	VkFramebufferCreateInfo framebuffer_info = {
	    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
	    .renderPass = rp,
	    .attachmentCount = 1,
	    .pAttachments = (VkImageView[]){self->framebuffers[eye].view},
	    .width = self->extent.width,
	    .height = self->extent.height,
	    .layers = 1,
	};

	res = vk->vkCreateFramebuffer(vk->device, &framebuffer_info, NULL, &self->framebuffers[eye].handle);
	vk_check_error("vkCreateFramebuffer", res, false);

	return true;
}

void
comp_layer_renderer_allocate_layers(struct comp_layer_renderer *self, uint32_t num_layers)
{
	struct vk_bundle *vk = self->vk;

	self->num_layers = num_layers;
	self->layers = U_TYPED_ARRAY_CALLOC(struct comp_render_layer *, self->num_layers);

	for (uint32_t i = 0; i < self->num_layers; i++) {
		self->layers[i] =
		    comp_layer_create(vk, &self->descriptor_set_layout, &self->descriptor_set_layout_equirect);
	}
}

void
comp_layer_renderer_destroy_layers(struct comp_layer_renderer *self)
{
	for (uint32_t i = 0; i < self->num_layers; i++)
		comp_layer_destroy(self->layers[i]);
	if (self->layers != NULL)
		free(self->layers);
	self->layers = NULL;
	self->num_layers = 0;
}

static bool
_init(
    struct comp_layer_renderer *self, struct comp_shaders *s, struct vk_bundle *vk, VkExtent2D extent, VkFormat format)
{
	self->vk = vk;

	self->nearZ = 0.001f;
	self->farZ = 100.0f;
	self->sample_count = VK_SAMPLE_COUNT_1_BIT;

	self->num_layers = 0;

	self->extent = extent;

	// binding indices used in layer.vert, layer.frag
	self->transformation_ubo_binding = 0;
	self->texture_binding = 1;

	for (uint32_t i = 0; i < 2; i++) {
		math_matrix_4x4_identity(&self->mat_projection[i]);
		math_matrix_4x4_identity(&self->mat_world_view[i]);
		math_matrix_4x4_identity(&self->mat_eye_view[i]);
	}

	if (!_init_render_pass(vk, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, self->sample_count,
	                       &self->render_pass))
		return false;

	for (uint32_t i = 0; i < 2; i++)
		if (!_init_frame_buffer(self, format, self->render_pass, i))
			return false;

	if (!_init_descriptor_layout(self))
		return false;
	if (!_init_descriptor_layout_equirect(self))
		return false;
	if (!_init_pipeline_layout(self))
		return false;
	if (!_init_pipeline_cache(self))
		return false;


	if (!_init_graphics_pipeline(self, s->layer_vert, s->layer_frag, false, &self->pipeline_premultiplied_alpha)) {
		return false;
	}

	if (!_init_graphics_pipeline(self, s->layer_vert, s->layer_frag, true, &self->pipeline_unpremultiplied_alpha)) {
		return false;
	}

	if (!_init_graphics_pipeline(self, s->equirect1_vert, s->equirect1_frag, true, &self->pipeline_equirect1)) {
		return false;
	}

	if (!_init_graphics_pipeline(self, s->equirect2_vert, s->equirect2_frag, true, &self->pipeline_equirect2)) {
		return false;
	}

	if (!_init_vertex_buffer(self))
		return false;

	return true;
}

struct comp_layer_renderer *
comp_layer_renderer_create(struct vk_bundle *vk, struct comp_shaders *s, VkExtent2D extent, VkFormat format)
{
	struct comp_layer_renderer *r = U_TYPED_CALLOC(struct comp_layer_renderer);
	_init(r, s, vk, extent, format);
	return r;
}

void
_render_pass_begin(struct vk_bundle *vk,
                   VkRenderPass render_pass,
                   VkExtent2D extent,
                   VkClearColorValue clear_color,
                   VkFramebuffer frame_buffer,
                   VkCommandBuffer cmd_buffer)
{
	VkRenderPassBeginInfo render_pass_info = {
	    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
	    .renderPass = render_pass,
	    .framebuffer = frame_buffer,
	    .renderArea =
	        {
	            .offset =
	                {
	                    .x = 0,
	                    .y = 0,
	                },
	            .extent = extent,
	        },
	    .clearValueCount = 1,
	    .pClearValues =
	        (VkClearValue[]){
	            {
	                .color = clear_color,
	            },
	            {
	                .depthStencil =
	                    {
	                        .depth = 1.0f,
	                        .stencil = 0,
	                    },
	            },
	        },
	};

	vk->vkCmdBeginRenderPass(cmd_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
}

static void
_render_stereo(struct comp_layer_renderer *self, struct vk_bundle *vk, VkCommandBuffer cmd_buffer)
{
	VkViewport viewport = {
	    0.0f, 0.0f, self->extent.width, self->extent.height, 0.0f, 1.0f,
	};
	vk->vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);
	VkRect2D scissor = {
	    .offset = {0, 0},
	    .extent = self->extent,
	};
	vk->vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

	for (uint32_t eye = 0; eye < 2; eye++) {
		_render_pass_begin(vk, self->render_pass, self->extent, background_color,
		                   self->framebuffers[eye].handle, cmd_buffer);

		_render_eye(self, eye, cmd_buffer, self->pipeline_layout);

		vk->vkCmdEndRenderPass(cmd_buffer);
	}
}

void
comp_layer_renderer_draw(struct comp_layer_renderer *self)
{
	struct vk_bundle *vk = self->vk;

	VkCommandBuffer cmd_buffer;
	if (vk_init_cmd_buffer(vk, &cmd_buffer) != VK_SUCCESS)
		return;

	os_mutex_lock(&vk->cmd_pool_mutex);
	_render_stereo(self, vk, cmd_buffer);
	os_mutex_unlock(&vk->cmd_pool_mutex);

	VkResult res = vk_submit_cmd_buffer(vk, cmd_buffer);
	vk_check_error("vk_submit_cmd_buffer", res, );
}

static void
_destroy_framebuffer(struct comp_layer_renderer *self, uint32_t i)
{
	struct vk_bundle *vk = self->vk;
	vk->vkDestroyImageView(vk->device, self->framebuffers[i].view, NULL);
	vk->vkDestroyImage(vk->device, self->framebuffers[i].image, NULL);
	vk->vkFreeMemory(vk->device, self->framebuffers[i].memory, NULL);
	vk->vkDestroyFramebuffer(vk->device, self->framebuffers[i].handle, NULL);
	vk->vkDestroySampler(vk->device, self->framebuffers[i].sampler, NULL);
}

void
comp_layer_renderer_destroy(struct comp_layer_renderer *self)
{
	struct vk_bundle *vk = self->vk;

	if (vk->device == VK_NULL_HANDLE)
		return;

	os_mutex_lock(&vk->queue_mutex);
	vk->vkDeviceWaitIdle(vk->device);
	os_mutex_unlock(&vk->queue_mutex);

	comp_layer_renderer_destroy_layers(self);

	for (uint32_t i = 0; i < 2; i++)
		_destroy_framebuffer(self, i);

	vk->vkDestroyRenderPass(vk->device, self->render_pass, NULL);

	vk->vkDestroyPipelineLayout(vk->device, self->pipeline_layout, NULL);
	vk->vkDestroyDescriptorSetLayout(vk->device, self->descriptor_set_layout, NULL);
	vk->vkDestroyDescriptorSetLayout(vk->device, self->descriptor_set_layout_equirect, NULL);
	vk->vkDestroyPipeline(vk->device, self->pipeline_premultiplied_alpha, NULL);
	vk->vkDestroyPipeline(vk->device, self->pipeline_unpremultiplied_alpha, NULL);
	vk->vkDestroyPipeline(vk->device, self->pipeline_equirect1, NULL);
	vk->vkDestroyPipeline(vk->device, self->pipeline_equirect2, NULL);

	for (uint32_t i = 0; i < ARRAY_SIZE(self->shader_modules); i++)
		vk->vkDestroyShaderModule(vk->device, self->shader_modules[i], NULL);

	vk_buffer_destroy(&self->vertex_buffer, vk);

	vk->vkDestroyPipelineCache(vk->device, self->pipeline_cache, NULL);
}

void
comp_layer_renderer_set_fov(struct comp_layer_renderer *self, const struct xrt_fov *fov, uint32_t eye)
{
	const float tan_left = tanf(fov->angle_left);
	const float tan_right = tanf(fov->angle_right);

	const float tan_down = tanf(fov->angle_down);
	const float tan_up = tanf(fov->angle_up);

	const float tan_width = tan_right - tan_left;
	const float tan_height = tan_up - tan_down;

	const float a11 = 2 / tan_width;
	const float a22 = 2 / tan_height;

	const float a31 = (tan_right + tan_left) / tan_width;
	const float a32 = (tan_up + tan_down) / tan_height;
	const float a33 = -self->farZ / (self->farZ - self->nearZ);

	const float a43 = -(self->farZ * self->nearZ) / (self->farZ - self->nearZ);

	// clang-format off
	self->mat_projection[eye] = (struct xrt_matrix_4x4) {
		.v = {
			a11, 0, 0, 0,
			0, a22, 0, 0,
			a31, a32, a33, -1,
			0, 0, a43, 0,
		}
	};
	// clang-format on
}

void
comp_layer_renderer_set_pose(struct comp_layer_renderer *self,
                             const struct xrt_pose *eye_pose,
                             const struct xrt_pose *world_pose,
                             uint32_t eye)
{
	math_matrix_4x4_view_from_pose(eye_pose, &self->mat_eye_view[eye]);
	math_matrix_4x4_view_from_pose(world_pose, &self->mat_world_view[eye]);
}
