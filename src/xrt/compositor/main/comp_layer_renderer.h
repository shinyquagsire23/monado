// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor quad rendering.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_main
 */

/*!
 * Holds associated vulkan objects and state to render quads.
 *
 * @ingroup comp_main
 */

#pragma once

#include "comp_layer.h"

struct comp_layer_renderer
{
	struct vk_bundle *vk;

	struct
	{
		VkImage image;
		VkDeviceMemory memory;
		VkImageView view;
		VkSampler sampler;
		VkFramebuffer handle;
	} framebuffers[2];

	VkRenderPass render_pass;

	VkExtent2D extent;

	VkSampleCountFlagBits sample_count;

	VkShaderModule shader_modules[2];
	VkPipeline pipeline_premultiplied_alpha;
	VkPipeline pipeline_unpremultiplied_alpha;
	VkPipeline pipeline_equirect1;
	VkPipeline pipeline_equirect2;
	VkDescriptorSetLayout descriptor_set_layout;
	VkDescriptorSetLayout descriptor_set_layout_equirect;

	VkPipelineLayout pipeline_layout;
	VkPipelineCache pipeline_cache;

	struct xrt_matrix_4x4 mat_world_view[2];
	struct xrt_matrix_4x4 mat_eye_view[2];
	struct xrt_matrix_4x4 mat_projection[2];

	struct vk_buffer vertex_buffer;

	float nearZ;
	float farZ;

	struct comp_render_layer **layers;
	uint32_t num_layers;

	uint32_t transformation_ubo_binding;
	uint32_t texture_binding;
};

struct comp_layer_renderer *
comp_layer_renderer_create(struct vk_bundle *vk, struct comp_shaders *s, VkExtent2D extent, VkFormat format);

void
comp_layer_renderer_destroy(struct comp_layer_renderer *self);

void
comp_layer_renderer_draw(struct comp_layer_renderer *self);

void
comp_layer_renderer_set_fov(struct comp_layer_renderer *self, const struct xrt_fov *fov, uint32_t eye);

void
comp_layer_renderer_set_pose(struct comp_layer_renderer *self,
                             const struct xrt_pose *eye_pose,
                             const struct xrt_pose *world_pose,
                             uint32_t eye);

void
comp_layer_renderer_allocate_layers(struct comp_layer_renderer *self, uint32_t num_layers);

void
comp_layer_renderer_destroy_layers(struct comp_layer_renderer *self);
