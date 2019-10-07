// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Distortion shader code header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp
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
 * @ingroup comp
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
 * @ingroup comp
 */
struct comp_distortion
{
	// Holds all of the needed common Vulkan things.
	struct vk_bundle *vk;

	struct comp_uniform_buffer ubo_handle;
	struct comp_uniform_buffer vbo_handle;
	struct comp_uniform_buffer ubo_viewport_handles[2];

	enum xrt_distortion_model distortion_model;

	struct
	{
		float hmd_warp_param[4];
		float aberr[4];
		float lens_center[2][4];
		float viewport_scale[2];
		float warp_scale;
	} ubo_pano;

	struct
	{
		float coefficients[2][3][4];
		float center[2][4];
		float undistort_r2_cutoff[4];
		float aspect_x_over_y;
		float grow_for_undistort;
	} ubo_vive;

	// vec2 for pos, vec2 for uv
	struct
	{
		void *data;
		size_t stride;
		size_t num;
	} vbo_mesh;

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
};


/*
 *
 * Functions.
 *
 */

/*!
 * Init a distortion, pass in the distortion so it can be embedded in a struct.
 *
 * @ingroup comp
 */
void
comp_distortion_init(struct comp_distortion *d,
                     struct comp_compositor *c,
                     VkRenderPass render_pass,
                     VkPipelineCache pipeline_cache,
                     enum xrt_distortion_model distortion_model,
                     struct xrt_hmd_parts *parts,
                     VkDescriptorPool descriptor_pool,
                     bool flip_y);

/*!
 * Free and destroy all fields, does not free the destortion itself.
 *
 * @ingroup comp
 */
void
comp_distortion_destroy(struct comp_distortion *d);

/*!
 * Update the descriptor set to a new image.
 *
 * @ingroup comp
 */
void
comp_distortion_update_descriptor_set(struct comp_distortion *d,
                                      VkSampler sampler,
                                      VkImageView view,
                                      uint32_t eye);

/*!
 * Submit draw commands to the given command_buffer.
 *
 * @ingroup comp
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
