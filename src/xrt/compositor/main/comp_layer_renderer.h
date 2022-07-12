// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor quad rendering.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "comp_layer.h"

/*!
 * Holds associated vulkan objects and state to render quads.
 *
 * @ingroup comp_main
 */
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
	VkPipeline pipeline_cube;
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
	uint32_t layer_count;

	uint32_t transformation_ubo_binding;
	uint32_t texture_binding;
};

/*!
 * Create a layer renderer.
 *
 * @public @memberof comp_layer_renderer
 */
struct comp_layer_renderer *
comp_layer_renderer_create(struct vk_bundle *vk, struct render_shaders *s, VkExtent2D extent, VkFormat format);

/*!
 * Destroy the layer renderer and set the pointer to NULL.
 *
 * @public @memberof comp_layer_renderer
 */
void
comp_layer_renderer_destroy(struct comp_layer_renderer **ptr_clr);

/*!
 * Perform draw calls for the layers.
 *
 * @param self Self pointer.
 *
 * @public @memberof comp_layer_renderer
 */
void
comp_layer_renderer_draw(struct comp_layer_renderer *self);

/*!
 * Update the internal members derived from the field of view.
 *
 * @param self Self pointer.
 * @param fov Field of view data
 * @param eye Eye index: 0 or 1
 *
 * @public @memberof comp_layer_renderer
 */
void
comp_layer_renderer_set_fov(struct comp_layer_renderer *self, const struct xrt_fov *fov, uint32_t eye);

/*!
 * Update the internal members derived from the eye and world poses.
 *
 * @param self Self pointer.
 * @param eye_pose Pose of eye in view
 * @param world_pose Pose of eye in world
 * @param eye Eye index: 0 or 1
 *
 * @public @memberof comp_layer_renderer
 */
void
comp_layer_renderer_set_pose(struct comp_layer_renderer *self,
                             const struct xrt_pose *eye_pose,
                             const struct xrt_pose *world_pose,
                             uint32_t eye);

/*!
 * Allocate the array comp_layer_renderer::layers with the given number of elements.
 *
 * @param self Self pointer.
 * @param layer_count The number of layers to support
 *
 * @public @memberof comp_layer_renderer
 */
void
comp_layer_renderer_allocate_layers(struct comp_layer_renderer *self, uint32_t layer_count);

/*!
 * De-initialize and free comp_layer_renderer::layers array.
 *
 * @param self Self pointer.
 *
 * @public @memberof comp_layer_renderer
 */
void
comp_layer_renderer_destroy_layers(struct comp_layer_renderer *self);
