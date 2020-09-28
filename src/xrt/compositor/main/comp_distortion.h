// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Distortion shader code header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "main/comp_settings.h"
#include "main/comp_compositor.h"


#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Structs
 *
 */


/*!
 * Helper buffer for a single uniform buffer.
 *
 * @ingroup comp_main
 */
struct comp_uniform_buffer
{
	VkDevice device;
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDescriptorBufferInfo descriptor;
	VkDeviceSize size;
	VkDeviceSize alignment;
	void *mapped;
	VkBufferUsageFlags usageFlags;
	VkMemoryPropertyFlags memoryPropertyFlags;
};

/*!
 * Helper struct that encapsulate a distortion rendering code.
 *
 * @ingroup comp_main
 */
struct comp_distortion
{
	// Holds all of the needed common Vulkan things.
	struct vk_bundle *vk;

	struct comp_uniform_buffer vbo_handle;
	struct comp_uniform_buffer index_handle;
	struct comp_uniform_buffer ubo_viewport_handles[2];
	uint32_t ubo_viewport_binding;

	enum xrt_distortion_model distortion_model;

	struct
	{
		float *vertices;
		int *indices;
		size_t stride;
		size_t num_vertices;
		size_t num_indices[2];
		size_t offset_indices[2];
		size_t total_num_indices;
	} mesh;

	struct
	{
		struct xrt_matrix_2x2 rot;
		int viewport_id;
		bool flip_y;
	} ubo_vp_data[2];

	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;

	VkDescriptorSetLayout descriptor_set_layout;
	VkDescriptorSet descriptor_sets[2];
	uint32_t render_texture_target_binding;

	bool quirk_draw_lines;
};


/*
 *
 * Functions.
 *
 */

/*!
 * Init a distortion, pass in the distortion so it can be embedded in a struct.
 *
 * @ingroup comp_main
 */
void
comp_distortion_init(struct comp_distortion *d,
                     struct comp_compositor *c,
                     VkRenderPass render_pass,
                     VkPipelineCache pipeline_cache,
                     enum xrt_distortion_model distortion_model,
                     struct xrt_hmd_parts *parts,
                     VkDescriptorPool descriptor_pool);

/*!
 * Free and destroy all fields, does not free the destortion itself.
 *
 * @ingroup comp_main
 */
void
comp_distortion_destroy(struct comp_distortion *d);

/*!
 * Update the descriptor set to a new image.
 *
 * @ingroup comp_main
 */
void
comp_distortion_update_descriptor_set(struct comp_distortion *d,
                                      VkSampler sampler,
                                      VkImageView view,
                                      uint32_t eye,
                                      bool flip_y);

/*!
 * Submit draw commands to the given command_buffer.
 *
 * @ingroup comp_main
 */
void
comp_distortion_draw_quad(struct comp_distortion *d,
                          VkCommandBuffer command_buffer,
                          int eye);

void
comp_distortion_draw_mesh(struct comp_distortion *d,
                          VkCommandBuffer command_buffer,
                          int eye);

#ifdef __cplusplus
}
#endif
