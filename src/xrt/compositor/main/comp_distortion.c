// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Distortion shader code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "main/comp_settings.h"
#include "main/comp_compositor.h"

#include "comp_distortion.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wnewline-eof"

#include "shaders/distortion.vert.h"
#include "shaders/mesh.frag.h"
#include "shaders/mesh.vert.h"
#include "shaders/none.frag.h"
#include "shaders/panotools.frag.h"
#include "shaders/vive.frag.h"


#pragma GCC diagnostic pop

/*
 *
 * Pre declare functions.
 *
 */

static void
comp_distortion_update_uniform_buffer_warp(struct comp_distortion *d,
                                           struct comp_compositor *c);

static void
comp_distortion_init_buffers(struct comp_distortion *d,
                             struct comp_compositor *c);

XRT_MAYBE_UNUSED static void
comp_distortion_update_descriptor_sets(struct comp_distortion *d,
                                       VkSampler samplers[2],
                                       VkImageView views[2],
                                       bool flip_y);

static void
comp_distortion_init_descriptor_set_layout(struct comp_distortion *d);

static void
comp_distortion_init_pipeline_layout(struct comp_distortion *d);

static void
comp_distortion_init_pipeline(struct comp_distortion *d,
                              VkRenderPass render_pass,
                              VkPipelineCache pipeline_cache);

static VkWriteDescriptorSet
comp_distortion_get_uniform_write_descriptor_set(struct comp_distortion *d,
                                                 uint32_t binding,
                                                 uint32_t eye);

static VkWriteDescriptorSet
comp_distortion_get_uniform_write_descriptor_set_vp(struct comp_distortion *d,
                                                    uint32_t binding,
                                                    uint32_t eye);

static VkWriteDescriptorSet
comp_distortion_get_image_write_descriptor_set(
    VkDescriptorSet descriptor_set,
    VkDescriptorImageInfo *descriptor_position,
    uint32_t binding);

static void
comp_distortion_init_descriptor_sets(struct comp_distortion *d,
                                     VkDescriptorPool descriptor_pool);


/*
 *
 * Buffer functions.
 *
 */

static void
_buffer_destroy(struct vk_bundle *vk, struct comp_uniform_buffer *buffer)
{
	if (buffer->buffer != VK_NULL_HANDLE) {
		vk->vkDestroyBuffer(buffer->device, buffer->buffer, NULL);
	}
	if (buffer->memory != VK_NULL_HANDLE) {
		vk->vkFreeMemory(buffer->device, buffer->memory, NULL);
	}
}

static VkResult
_buffer_map(struct vk_bundle *vk,
            struct comp_uniform_buffer *buffer,
            VkDeviceSize size,
            VkDeviceSize offset)
{
	return vk->vkMapMemory(vk->device, buffer->memory, offset, size, 0,
	                       &buffer->mapped);
}

static void
_buffer_unmap(struct vk_bundle *vk, struct comp_uniform_buffer *buffer)
{
	if (buffer->mapped) {
		vk->vkUnmapMemory(vk->device, buffer->memory);
		buffer->mapped = NULL;
	}
}

static void
_buffer_setup_descriptor(struct vk_bundle *vk,
                         struct comp_uniform_buffer *buffer,
                         VkDeviceSize size,
                         VkDeviceSize offset)
{
	buffer->descriptor.offset = offset;
	buffer->descriptor.buffer = buffer->buffer;
	buffer->descriptor.range = size;
}


/*
 *
 * Shader functions.
 *
 */

static VkPipelineShaderStageCreateInfo
_shader_load(struct vk_bundle *vk,
             const uint32_t *code,
             size_t size,
             VkShaderStageFlagBits flags)
{
	VkResult ret;

	VkShaderModuleCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	    .codeSize = size,
	    .pCode = code,
	};

	VkShaderModule module;
	ret = vk->vkCreateShaderModule(vk->device, &info, NULL, &module);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "vkCreateShaderModule failed %u", ret);
	}

	return (VkPipelineShaderStageCreateInfo){
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
	    .stage = flags,
	    .module = module,
	    .pName = "main",
	};
}


/*
 *
 * Functions.
 *
 */

void
comp_distortion_init(struct comp_distortion *d,
                     struct comp_compositor *c,
                     VkRenderPass render_pass,
                     VkPipelineCache pipeline_cache,
                     enum xrt_distortion_model distortion_model,
                     struct xrt_hmd_parts *parts,
                     VkDescriptorPool descriptor_pool)
{
	d->vk = &c->vk;

	d->distortion_model = distortion_model;

	//! Add support for 1 channels as well.
	assert(parts->distortion.mesh.vertices == NULL ||
	       parts->distortion.mesh.num_uv_channels == 3);
	assert(parts->distortion.mesh.indices == NULL ||
	       parts->distortion.mesh.total_num_indices != 0);
	assert(parts->distortion.mesh.indices == NULL ||
	       parts->distortion.mesh.num_indices[0] != 0);
	assert(parts->distortion.mesh.indices == NULL ||
	       parts->distortion.mesh.num_indices[1] != 0);

	d->mesh.vertices = parts->distortion.mesh.vertices;
	d->mesh.stride = parts->distortion.mesh.stride;
	d->mesh.num_vertices = parts->distortion.mesh.num_vertices;
	d->mesh.indices = parts->distortion.mesh.indices;
	d->mesh.total_num_indices = parts->distortion.mesh.total_num_indices;
	d->mesh.num_indices[0] = parts->distortion.mesh.num_indices[0];
	d->mesh.num_indices[1] = parts->distortion.mesh.num_indices[1];
	d->mesh.offset_indices[0] = parts->distortion.mesh.offset_indices[0];
	d->mesh.offset_indices[1] = parts->distortion.mesh.offset_indices[1];

	d->ubo_vp_data[0].flip_y = false;
	d->ubo_vp_data[1].flip_y = false;
	d->quirk_draw_lines = c->settings.debug.wireframe;

	comp_distortion_init_buffers(d, c);
	comp_distortion_update_uniform_buffer_warp(d, c);
	comp_distortion_init_descriptor_set_layout(d);
	comp_distortion_init_pipeline_layout(d);
	comp_distortion_init_pipeline(d, render_pass, pipeline_cache);
	comp_distortion_init_descriptor_sets(d, descriptor_pool);
}

void
comp_distortion_destroy(struct comp_distortion *d)
{
	struct vk_bundle *vk = d->vk;

	/*
	 * This makes sure that any pending command buffer has completed and all
	 * resources referred by it can now be manipulated. This make sure that
	 * validation doesn't complain. This is done during destroy so isn't
	 * time critical.
	 */
	vk->vkDeviceWaitIdle(vk->device);

	vk->vkDestroyDescriptorSetLayout(vk->device, d->descriptor_set_layout,
	                                 NULL);

	if (d->has_fragment_shader_ubo) {
		_buffer_destroy(vk, &d->ubo_handle);
	}
	_buffer_destroy(vk, &d->vbo_handle);
	_buffer_destroy(vk, &d->index_handle);
	_buffer_destroy(vk, &d->ubo_viewport_handles[0]);
	_buffer_destroy(vk, &d->ubo_viewport_handles[1]);

	vk->vkDestroyPipeline(vk->device, d->pipeline, NULL);
	vk->vkDestroyPipelineLayout(vk->device, d->pipeline_layout, NULL);

	free(d);
}

static void
comp_distortion_init_pipeline(struct comp_distortion *d,
                              VkRenderPass render_pass,
                              VkPipelineCache pipeline_cache)
{
	struct vk_bundle *vk = d->vk;
	VkResult ret;

	VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
	if (d->quirk_draw_lines) {
		polygonMode = VK_POLYGON_MODE_LINE;
	}

	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	if (d->mesh.total_num_indices > 0) {
		topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	}

	VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
	    .sType =
	        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
	    .topology = topology,
	    .primitiveRestartEnable = VK_FALSE,
	};

	VkPipelineRasterizationStateCreateInfo rasterization_state = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
	    .depthClampEnable = VK_FALSE,
	    .rasterizerDiscardEnable = VK_FALSE,
	    .polygonMode = polygonMode,
	    .cullMode = VK_CULL_MODE_BACK_BIT,
	    .frontFace = VK_FRONT_FACE_CLOCKWISE,
	    .lineWidth = 1.0f,
	};

	VkPipelineColorBlendAttachmentState blend_attachment_state = {
	    .blendEnable = VK_FALSE,
	    .colorWriteMask = 0xf,
	};

	VkPipelineColorBlendStateCreateInfo color_blend_state = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
	    .attachmentCount = 1,
	    .pAttachments = &blend_attachment_state,
	};

	VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
	    .depthTestEnable = VK_TRUE,
	    .depthWriteEnable = VK_TRUE,
	    .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
	    .front =
	        {
	            .compareOp = VK_COMPARE_OP_ALWAYS,
	        },
	    .back =
	        {
	            .compareOp = VK_COMPARE_OP_ALWAYS,
	        },
	};

	VkPipelineViewportStateCreateInfo viewport_state = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
	    .viewportCount = 1,
	    .scissorCount = 1,
	};

	VkPipelineMultisampleStateCreateInfo multisample_state = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
	    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

	VkDynamicState dynamic_states[] = {
	    VK_DYNAMIC_STATE_VIEWPORT,
	    VK_DYNAMIC_STATE_SCISSOR,
	};

	VkPipelineDynamicStateCreateInfo dynamic_state = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
	    .dynamicStateCount = 2,
	    .pDynamicStates = dynamic_states,
	};


	VkVertexInputBindingDescription vertex_input_binding_description;
	VkVertexInputAttributeDescription
	    vertex_input_attribute_descriptions[2];

	const uint32_t *fragment_shader_code;
	size_t fragment_shader_size;

	/*
	 * By default, we will generate positions and UVs for the full screen
	 * quad from the gl_VertexIndex
	 */
	VkPipelineVertexInputStateCreateInfo vertex_input_state = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};
	const uint32_t *vertex_shader_code = shaders_distortion_vert;
	size_t vertex_shader_size = sizeof(shaders_distortion_vert);

	switch (d->distortion_model) {
	case XRT_DISTORTION_MODEL_NONE:
		fragment_shader_code = shaders_none_frag;
		fragment_shader_size = sizeof(shaders_none_frag);
		break;
	case XRT_DISTORTION_MODEL_OPENHMD:
		fragment_shader_code = shaders_panotools_frag;
		fragment_shader_size = sizeof(shaders_panotools_frag);
		break;
	case XRT_DISTORTION_MODEL_VIVE:
		fragment_shader_code = shaders_vive_frag;
		fragment_shader_size = sizeof(shaders_vive_frag);
		break;
	case XRT_DISTORTION_MODEL_MESHUV:
		// clang-format off
		vertex_input_attribute_descriptions[0].binding = 0;
		vertex_input_attribute_descriptions[0].location = 0;
		vertex_input_attribute_descriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		vertex_input_attribute_descriptions[0].offset = 0;

		vertex_input_attribute_descriptions[1].binding = 0;
		vertex_input_attribute_descriptions[1].location = 1;
		vertex_input_attribute_descriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		vertex_input_attribute_descriptions[1].offset = 16;

		vertex_input_binding_description.binding = 0;
		vertex_input_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		vertex_input_binding_description.stride = d->mesh.stride;

		vertex_input_state.vertexAttributeDescriptionCount = 2;
		vertex_input_state.pVertexAttributeDescriptions = vertex_input_attribute_descriptions;
		vertex_input_state.vertexBindingDescriptionCount = 1;
		vertex_input_state.pVertexBindingDescriptions = &vertex_input_binding_description;
		// clang-format on

		vertex_shader_code = shaders_mesh_vert;
		vertex_shader_size = sizeof(shaders_mesh_vert);
		fragment_shader_code = shaders_mesh_frag;
		fragment_shader_size = sizeof(shaders_mesh_frag);
		break;
	default:
		fragment_shader_code = shaders_panotools_frag;
		fragment_shader_size = sizeof(shaders_panotools_frag);
		break;
	}

	VkPipelineShaderStageCreateInfo shader_stages[2] = {
	    _shader_load(d->vk, vertex_shader_code, vertex_shader_size,
	                 VK_SHADER_STAGE_VERTEX_BIT),
	    _shader_load(d->vk, fragment_shader_code, fragment_shader_size,
	                 VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	VkGraphicsPipelineCreateInfo pipeline_info = {
	    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
	    .stageCount = ARRAY_SIZE(shader_stages),
	    .pStages = shader_stages,
	    .pVertexInputState = &vertex_input_state,
	    .pInputAssemblyState = &input_assembly_state,
	    .pViewportState = &viewport_state,
	    .pRasterizationState = &rasterization_state,
	    .pMultisampleState = &multisample_state,
	    .pDepthStencilState = &depth_stencil_state,
	    .pColorBlendState = &color_blend_state,
	    .pDynamicState = &dynamic_state,
	    .layout = d->pipeline_layout,
	    .renderPass = render_pass,
	    .basePipelineHandle = VK_NULL_HANDLE,
	    .basePipelineIndex = -1,
	};

	ret = vk->vkCreateGraphicsPipelines(vk->device, pipeline_cache, 1,
	                                    &pipeline_info, NULL, &d->pipeline);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(d->vk, "vkCreateGraphicsPipelines failed %u!", ret);
	}

	vk->vkDestroyShaderModule(vk->device, shader_stages[0].module, NULL);
	vk->vkDestroyShaderModule(vk->device, shader_stages[1].module, NULL);
}

static VkWriteDescriptorSet
comp_distortion_get_uniform_write_descriptor_set(struct comp_distortion *d,
                                                 uint32_t binding,
                                                 uint32_t eye)
{
	return (VkWriteDescriptorSet){
	    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet = d->descriptor_sets[eye],
	    .dstBinding = binding,
	    .descriptorCount = 1,
	    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	    .pBufferInfo = &d->ubo_handle.descriptor,
	};
}

static VkWriteDescriptorSet
comp_distortion_get_uniform_write_descriptor_set_vp(struct comp_distortion *d,
                                                    uint32_t binding,
                                                    uint32_t eye)
{
	return (VkWriteDescriptorSet){
	    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet = d->descriptor_sets[eye],
	    .dstBinding = binding,
	    .descriptorCount = 1,
	    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	    .pBufferInfo = &d->ubo_viewport_handles[eye].descriptor,
	};
}

static VkWriteDescriptorSet
comp_distortion_get_image_write_descriptor_set(
    VkDescriptorSet descriptor_set,
    VkDescriptorImageInfo *descriptor_position,
    uint32_t binding)
{
	return (VkWriteDescriptorSet){
	    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet = descriptor_set,
	    .dstBinding = binding,
	    .descriptorCount = 1,
	    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	    .pImageInfo = descriptor_position,
	};
}

static void
comp_distortion_init_descriptor_sets(struct comp_distortion *d,
                                     VkDescriptorPool descriptor_pool)
{
	struct vk_bundle *vk = d->vk;
	VkResult ret;

	VkDescriptorSetAllocateInfo alloc_info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
	    .descriptorPool = descriptor_pool,
	    .descriptorSetCount = 1,
	    .pSetLayouts = &d->descriptor_set_layout,
	};

	for (uint32_t i = 0; i < 2; i++) {
		ret = vk->vkAllocateDescriptorSets(d->vk->device, &alloc_info,
		                                   &d->descriptor_sets[i]);
		if (ret != VK_SUCCESS) {
			VK_DEBUG(d->vk, "vkAllocateDescriptorSets failed %u",
			         ret);
		}
	}
}

void
comp_distortion_update_descriptor_set(struct comp_distortion *d,
                                      VkSampler sampler,
                                      VkImageView view,
                                      uint32_t eye,
                                      bool flip_y)
{
	struct vk_bundle *vk = d->vk;

	VkDescriptorImageInfo image_info = {
	    .sampler = sampler,
	    .imageView = view,
	    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	if (d->has_fragment_shader_ubo) {
		VkWriteDescriptorSet write_descriptor_sets[3] = {
		    // Binding 0 : Render texture target
		    comp_distortion_get_image_write_descriptor_set(
		        d->descriptor_sets[eye], &image_info, 0),
		    // Binding 1 : Fragment shader uniform buffer
		    comp_distortion_get_uniform_write_descriptor_set(d, 1, eye),
		    // Binding 2 : view uniform buffer
		    comp_distortion_get_uniform_write_descriptor_set_vp(d, 2,
		                                                        eye),
		};

		vk->vkUpdateDescriptorSets(vk->device,
		                           ARRAY_SIZE(write_descriptor_sets),
		                           write_descriptor_sets, 0, NULL);
	} else {
		VkWriteDescriptorSet write_descriptor_sets[2] = {
		    // Binding 0 : Render texture target
		    comp_distortion_get_image_write_descriptor_set(
		        d->descriptor_sets[eye], &image_info, 0),
		    // Binding 2 : view uniform buffer
		    comp_distortion_get_uniform_write_descriptor_set_vp(d, 2,
		                                                        eye),
		};

		vk->vkUpdateDescriptorSets(vk->device,
		                           ARRAY_SIZE(write_descriptor_sets),
		                           write_descriptor_sets, 0, NULL);
	}

	d->ubo_vp_data[eye].flip_y = flip_y;
	memcpy(d->ubo_viewport_handles[eye].mapped, &d->ubo_vp_data[eye],
	       sizeof(d->ubo_vp_data[eye]));
}

static void
comp_distortion_update_descriptor_sets(struct comp_distortion *d,
                                       VkSampler samplers[2],
                                       VkImageView views[2],
                                       bool flip_y)
{
	for (uint32_t i = 0; i < 2; i++) {
		comp_distortion_update_descriptor_set(d, samplers[i], views[i],
		                                      i, flip_y);
	}
}

static void
comp_distortion_init_descriptor_set_layout(struct comp_distortion *d)
{
	struct vk_bundle *vk = d->vk;
	VkResult ret;

	VkDescriptorSetLayoutBinding set_layout_bindings[3] = {
	    // Binding 0 : Render texture target left
	    {
	        .binding = 0,
	        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .descriptorCount = 1,
	        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	    },
	    // Binding 1 : Fragment shader uniform buffer
	    {
	        .binding = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	        .descriptorCount = 1,
	        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	    },
	    // binding 2: viewport index
	    {
	        .binding = 2,
	        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	        .descriptorCount = 1,
	        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	    },
	};

	VkDescriptorSetLayoutCreateInfo set_layout_info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	    .bindingCount = ARRAY_SIZE(set_layout_bindings),
	    .pBindings = set_layout_bindings,
	};

	ret = vk->vkCreateDescriptorSetLayout(d->vk->device, &set_layout_info,
	                                      NULL, &d->descriptor_set_layout);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(d->vk, "vkCreateDescriptorSetLayout failed %u", ret);
	}
}

static void
comp_distortion_init_pipeline_layout(struct comp_distortion *d)
{
	struct vk_bundle *vk = d->vk;
	VkResult ret;

	VkPipelineLayoutCreateInfo pipeline_layout_info = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	    .setLayoutCount = 1,
	    .pSetLayouts = &d->descriptor_set_layout,
	};

	ret = vk->vkCreatePipelineLayout(d->vk->device, &pipeline_layout_info,
	                                 NULL, &d->pipeline_layout);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(d->vk, "Failed to create pipeline layout!");
	}
}

void
comp_distortion_draw_quad(struct comp_distortion *d,
                          VkCommandBuffer command_buffer,
                          int eye)
{
	struct vk_bundle *vk = d->vk;

	vk->vkCmdBindDescriptorSets(
	    command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, d->pipeline_layout,
	    0, 1, &d->descriptor_sets[eye], 0, NULL);

	vk->vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                      d->pipeline);

	/* Draw 3 verts from which we construct the fullscreen quad in
	 * the shader*/
	vk->vkCmdDraw(command_buffer, 3, 1, 0, 0);
}

void
comp_distortion_draw_mesh(struct comp_distortion *d,
                          VkCommandBuffer command_buffer,
                          int eye)
{
	struct vk_bundle *vk = d->vk;


	vk->vkCmdBindDescriptorSets(
	    command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, d->pipeline_layout,
	    0, 1, &d->descriptor_sets[eye], 0, NULL);
	vk->vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                      d->pipeline);

	VkDeviceSize offsets[] = {0};
	vk->vkCmdBindVertexBuffers(command_buffer, 0, 1,
	                           &(d->vbo_handle.buffer), offsets);

	if (d->mesh.total_num_indices > 0) {
		vk->vkCmdBindIndexBuffer(command_buffer, d->index_handle.buffer,
		                         0, VK_INDEX_TYPE_UINT32);

		vk->vkCmdDrawIndexed(command_buffer, d->mesh.num_indices[eye],
		                     1, d->mesh.offset_indices[eye], 0, 0);

	} else {
		vk->vkCmdDraw(command_buffer, d->mesh.num_vertices, 1, 0, 0);
	}
}

// Update fragment shader hmd warp uniform block
static void
comp_distortion_update_uniform_buffer_warp(struct comp_distortion *d,
                                           struct comp_compositor *c)
{
	// clang-format off
	switch (d->distortion_model) {
	case XRT_DISTORTION_MODEL_VIVE:
		/*
		 * VIVE fragment shader
		 */
		d->ubo_vive.aspect_x_over_y = c->xdev->hmd->distortion.vive.aspect_x_over_y;
		d->ubo_vive.grow_for_undistort = c->xdev->hmd->distortion.vive.grow_for_undistort;

		for (uint32_t i = 0; i < 2; i++)
			d->ubo_vive.undistort_r2_cutoff[i] = c->xdev->hmd->distortion.vive.undistort_r2_cutoff[i];

		for (uint32_t i = 0; i < 2; i++)
			for (uint32_t j = 0; j < 2; j++)
				d->ubo_vive.center[i][j] = c->xdev->hmd->distortion.vive.center[i][j];

		for (uint32_t i = 0; i < 2; i++)
			for (uint32_t j = 0; j < 3; j++)
				for (uint32_t k = 0; k < 3; k++)
					d->ubo_vive.coefficients[i][j][k] = c->xdev->hmd->distortion.vive.coefficients[i][j][k];

		memcpy(d->ubo_handle.mapped, &d->ubo_vive, sizeof(d->ubo_vive));
		break;
	case XRT_DISTORTION_MODEL_MESHUV:
		break;
	case XRT_DISTORTION_MODEL_NONE:
		break;
	case XRT_DISTORTION_MODEL_PANOTOOLS:
		break;
	case XRT_DISTORTION_MODEL_OPENHMD:
	default:
		/*
		 * Pano vision fragment shader
		 */
		d->ubo_pano.hmd_warp_param[0] = c->xdev->hmd->distortion.openhmd.distortion_k[0];
		d->ubo_pano.hmd_warp_param[1] = c->xdev->hmd->distortion.openhmd.distortion_k[1];
		d->ubo_pano.hmd_warp_param[2] = c->xdev->hmd->distortion.openhmd.distortion_k[2];
		d->ubo_pano.hmd_warp_param[3] = c->xdev->hmd->distortion.openhmd.distortion_k[3];
		d->ubo_pano.aberr[0] = c->xdev->hmd->distortion.openhmd.aberration_k[0];
		d->ubo_pano.aberr[1] = c->xdev->hmd->distortion.openhmd.aberration_k[1];
		d->ubo_pano.aberr[2] = c->xdev->hmd->distortion.openhmd.aberration_k[2];
		d->ubo_pano.aberr[3] = c->xdev->hmd->distortion.openhmd.aberration_k[3];
		d->ubo_pano.lens_center[0][0] = c->xdev->hmd->views[0].lens_center.x_meters;
		d->ubo_pano.lens_center[0][1] = c->xdev->hmd->views[0].lens_center.y_meters;
		d->ubo_pano.lens_center[1][0] = c->xdev->hmd->views[1].lens_center.x_meters;
		d->ubo_pano.lens_center[1][1] = c->xdev->hmd->views[1].lens_center.y_meters;
		d->ubo_pano.viewport_scale[0] = c->xdev->hmd->views[0].display.w_meters;
		d->ubo_pano.viewport_scale[1] = c->xdev->hmd->views[0].display.h_meters;
		d->ubo_pano.warp_scale = c->xdev->hmd->distortion.openhmd.warp_scale;

		memcpy(d->ubo_handle.mapped, &d->ubo_pano, sizeof(d->ubo_pano));
		break;
	}
	// clang-format on

	/*
	 * Common vertex shader stuff.
	 */

	// clang-format off
	d->ubo_vp_data[0].viewport_id = 0;
	d->ubo_vp_data[0].rot = c->xdev->hmd->views[0].rot;
	d->ubo_vp_data[1].viewport_id = 1;
	d->ubo_vp_data[1].rot = c->xdev->hmd->views[1].rot;

	memcpy(d->ubo_viewport_handles[0].mapped, &d->ubo_vp_data[0], sizeof(d->ubo_vp_data[0]));
	memcpy(d->ubo_viewport_handles[1].mapped, &d->ubo_vp_data[1], sizeof(d->ubo_vp_data[1]));
	// clang-format on
}

static VkResult
_create_buffer(struct vk_bundle *vk,
               VkBufferUsageFlags usage_flags,
               VkMemoryPropertyFlags memory_property_flags,
               struct comp_uniform_buffer *buffer,
               VkDeviceSize size,
               void *data)
{
	buffer->device = vk->device;
	VkResult ret;

	// Create the buffer handle.
	VkBufferCreateInfo buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .size = size,
	    .usage = usage_flags,
	};
	ret =
	    vk->vkCreateBuffer(vk->device, &buffer_info, NULL, &buffer->buffer);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "Failed to create buffer!");
		return ret;
	}

	// Create the memory backing up the buffer handle.
	VkMemoryRequirements mem_reqs;
	vk->vkGetBufferMemoryRequirements(vk->device, buffer->buffer,
	                                  &mem_reqs);

	// Find a memory type index that fits the properties of the buffer.
	uint32_t memory_type_index = 0;
	vk_get_memory_type(vk, mem_reqs.memoryTypeBits, memory_property_flags,
	                   &memory_type_index);

	VkMemoryAllocateInfo mem_alloc = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	    .allocationSize = mem_reqs.size,
	    .memoryTypeIndex = memory_type_index,
	};

	ret =
	    vk->vkAllocateMemory(vk->device, &mem_alloc, NULL, &buffer->memory);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "Failed to allocate memory!");
		goto err_buffer;
	}

	buffer->alignment = mem_reqs.alignment;
	buffer->size = mem_alloc.allocationSize;
	buffer->usageFlags = usage_flags;
	buffer->memoryPropertyFlags = memory_property_flags;

	// If a pointer to the buffer data has been passed, map the
	// buffer and copy over the data
	if (data != NULL) {
		ret = _buffer_map(vk, buffer, VK_WHOLE_SIZE, 0);
		if (ret != VK_SUCCESS) {
			VK_DEBUG(vk, "Failed to map buffer!");
			goto err_memory;
		}

		memcpy(buffer->mapped, data, size);
		_buffer_unmap(vk, buffer);
	}

	// Initialize a default descriptor that covers the whole buffer size
	_buffer_setup_descriptor(vk, buffer, VK_WHOLE_SIZE, 0);

	// Attach the memory to the buffer object
	ret = vk->vkBindBufferMemory(vk->device, buffer->buffer, buffer->memory,
	                             0);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "Failed to bind buffer to memory!");
		goto err_memory;
	}

	return VK_SUCCESS;


err_memory:
	vk->vkFreeMemory(vk->device, buffer->memory, NULL);

err_buffer:
	vk->vkDestroyBuffer(vk->device, buffer->buffer, NULL);

	return ret;
}

static void
comp_distortion_init_buffers(struct comp_distortion *d,
                             struct comp_compositor *c)
{
	struct vk_bundle *vk = &c->vk;
	VkMemoryPropertyFlags memory_property_flags = 0;
	VkBufferUsageFlags ubo_usage_flags = 0;
	VkBufferUsageFlags vbo_usage_flags = 0;
	VkBufferUsageFlags index_usage_flags = 0;

	VkResult ret;

	// Using the same flags for all ubos and vbos uniform buffers.
	ubo_usage_flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	vbo_usage_flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	index_usage_flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	memory_property_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
	memory_property_flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	// Distortion ubo and vbo sizes.
	VkDeviceSize ubo_size = 0;
	VkDeviceSize vbo_size = 0;
	VkDeviceSize index_size = 0;

	// overridden for mesh distortion in switch below
	d->has_fragment_shader_ubo = true;

	switch (d->distortion_model) {
	case XRT_DISTORTION_MODEL_OPENHMD:
		ubo_size = sizeof(d->ubo_pano);
		break;
	case XRT_DISTORTION_MODEL_MESHUV:
		d->has_fragment_shader_ubo = false;
		vbo_size = d->mesh.stride * d->mesh.num_vertices;
		index_size = sizeof(int) * d->mesh.total_num_indices;
		break;
	case XRT_DISTORTION_MODEL_VIVE:
		// Vive data
		ubo_size = sizeof(d->ubo_vive);
		break;
	default:
		// Should this be a error?
		ubo_size = sizeof(d->ubo_pano);
		break;
	}

	if (d->has_fragment_shader_ubo) {
		// fp ubo
		ret = _create_buffer(vk, ubo_usage_flags, memory_property_flags,
		                     &d->ubo_handle, ubo_size, NULL);
		if (ret != VK_SUCCESS) {
			VK_DEBUG(vk, "Failed to create warp ubo buffer!");
		}
		ret = _buffer_map(vk, &d->ubo_handle, VK_WHOLE_SIZE, 0);
		if (ret != VK_SUCCESS) {
			VK_DEBUG(vk, "Failed to map warp ubo buffer!");
		}
	}

	// vp ubo[0]
	ret = _create_buffer(vk, ubo_usage_flags, memory_property_flags,
	                     &d->ubo_viewport_handles[0],
	                     sizeof(d->ubo_vp_data[0]), NULL);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "Failed to create vp ubo buffer[0]!");
	}
	ret = _buffer_map(vk, &d->ubo_viewport_handles[0], VK_WHOLE_SIZE, 0);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "Failed to map vp ubo buffer[0]!");
	}

	// vp ubo[1]
	ret = _create_buffer(vk, ubo_usage_flags, memory_property_flags,
	                     &d->ubo_viewport_handles[1],
	                     sizeof(d->ubo_vp_data[1]), NULL);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "Failed to create vp ubo buffer[1]!");
	}
	ret = _buffer_map(vk, &d->ubo_viewport_handles[1], VK_WHOLE_SIZE, 0);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "Failed to map vp ubo buffer[1]!");
	}

	// Don't create vbo if size is zero.
	if (vbo_size == 0) {
		return;
	}

	ret = _create_buffer(vk, vbo_usage_flags, memory_property_flags,
	                     &d->vbo_handle, vbo_size, d->mesh.vertices);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "Failed to create mesh vbo buffer!");
	}
	ret = _buffer_map(vk, &d->vbo_handle, vbo_size, 0);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "Failed to map mesh vbo buffer!");
	}

	if (index_size == 0) {
		return;
	}

	ret = _create_buffer(vk, index_usage_flags, memory_property_flags,
	                     &d->index_handle, index_size, d->mesh.indices);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "Failed to create mesh index buffer!");
	}
	ret = _buffer_map(vk, &d->index_handle, index_size, 0);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "Failed to map mesh vbo buffer!");
	}
}
