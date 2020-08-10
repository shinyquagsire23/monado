// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor quad rendering.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_main
 */

#include "comp_layer.h"

#include "util/u_misc.h"
#include "math/m_api.h"

#include <stdio.h>

// clang-format off
// Projection layers span from -1 to 1, the vertex buffer and quad layers
// from -0.5 to 0.5, so this scale matrix needs to be applied for proj layers.
static struct xrt_matrix_4x4 proj_scale = {
	.v = {
		2, 0, 0, 0,
		0, 2, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	}
};
// clang-format on

void
comp_layer_set_flip_y(struct comp_render_layer *self, bool flip_y)
{
	for (uint32_t i = 0; i < 2; i++)
		self->transformation[i].flip_y = flip_y;
}

void
comp_layer_set_model_matrix(struct comp_render_layer *self,
                            const struct xrt_matrix_4x4 *m)
{
	memcpy(&self->model_matrix, m, sizeof(struct xrt_matrix_4x4));
}

static void
_update_mvp_matrix(struct comp_render_layer *self,
                   uint32_t eye,
                   const struct xrt_matrix_4x4 *vp)
{
	math_matrix_4x4_multiply(vp, &self->model_matrix,
	                         &self->transformation[eye].mvp);
	memcpy(self->transformation_ubos[eye].data, &self->transformation[eye],
	       sizeof(struct layer_transformation));
}

static bool
_init_ubos(struct comp_render_layer *self)
{
	VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	VkMemoryPropertyFlags properties =
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
	    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
	    VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

	for (uint32_t i = 0; i < 2; i++) {
		math_matrix_4x4_identity(&self->transformation[i].mvp);

		if (!vk_buffer_init(
		        self->vk, sizeof(struct layer_transformation), usage,
		        properties, &self->transformation_ubos[i].handle,
		        &self->transformation_ubos[i].memory))
			return false;

		VkResult res = self->vk->vkMapMemory(
		    self->vk->device, self->transformation_ubos[i].memory, 0,
		    VK_WHOLE_SIZE, 0, &self->transformation_ubos[i].data);
		vk_check_error("vkMapMemory", res, false);

		memcpy(self->transformation_ubos[i].data,
		       &self->transformation[i],
		       sizeof(struct layer_transformation));
	}
	return true;
}

static void
_update_descriptor(struct vk_bundle *vk,
                   VkDescriptorSet set,
                   VkBuffer transformation_buffer,
                   VkSampler sampler,
                   VkImageView image_view)
{
	VkWriteDescriptorSet *sets = (VkWriteDescriptorSet[]){
	    {
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = set,
	        .dstBinding = 0,
	        .descriptorCount = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	        .pBufferInfo =
	            &(VkDescriptorBufferInfo){
	                .buffer = transformation_buffer,
	                .offset = 0,
	                .range = VK_WHOLE_SIZE,
	            },
	        .pTexelBufferView = NULL,
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = set,
	        .dstBinding = 1,
	        .descriptorCount = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .pImageInfo =
	            &(VkDescriptorImageInfo){
	                .sampler = sampler,
	                .imageView = image_view,
	                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	            },
	        .pBufferInfo = NULL,
	        .pTexelBufferView = NULL,
	    },
	};

	vk->vkUpdateDescriptorSets(vk->device, 2, sets, 0, NULL);
}

void
comp_layer_update_descriptors(struct comp_render_layer *self,
                              VkSampler sampler,
                              VkImageView image_view)
{
	for (uint32_t eye = 0; eye < 2; eye++)
		_update_descriptor(self->vk, self->descriptor_sets[eye],
		                   self->transformation_ubos[eye].handle,
		                   sampler, image_view);
}

void
comp_layer_update_stereo_descriptors(struct comp_render_layer *self,
                                     VkSampler left_sampler,
                                     VkSampler right_sampler,
                                     VkImageView left_image_view,
                                     VkImageView right_image_view)
{
	_update_descriptor(self->vk, self->descriptor_sets[0],
	                   self->transformation_ubos[0].handle, left_sampler,
	                   left_image_view);

	_update_descriptor(self->vk, self->descriptor_sets[1],
	                   self->transformation_ubos[1].handle, right_sampler,
	                   right_image_view);
}

static bool
_init(struct comp_render_layer *self,
      struct vk_bundle *vk,
      VkDescriptorSetLayout *layout)
{
	self->vk = vk;

	self->view_space = true;
	self->visibility = XRT_LAYER_EYE_VISIBILITY_BOTH;

	math_matrix_4x4_identity(&self->model_matrix);

	if (!_init_ubos(self))
		return false;

	uint32_t set_count = 2;

	VkDescriptorPoolSize pool_sizes[] = {
	    {
	        .descriptorCount = set_count,
	        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	    },
	    {
	        .descriptorCount = set_count,
	        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	    },
	};

	if (!vk_init_descriptor_pool(self->vk, pool_sizes,
	                             ARRAY_SIZE(pool_sizes), set_count,
	                             &self->descriptor_pool))
		return false;

	for (uint32_t eye = 0; eye < set_count; eye++)
		if (!vk_allocate_descriptor_sets(
		        self->vk, self->descriptor_pool, 1, layout,
		        &self->descriptor_sets[eye]))
			return false;

	return true;
}

void
comp_layer_draw(struct comp_render_layer *self,
                uint32_t eye,
                VkPipeline pipeline,
                VkPipelineLayout pipeline_layout,
                VkCommandBuffer cmd_buffer,
                const struct vk_buffer *vertex_buffer,
                const struct xrt_matrix_4x4 *vp_world,
                const struct xrt_matrix_4x4 *vp_eye)
{
	if (eye == 0 &&
	    (self->visibility & XRT_LAYER_EYE_VISIBILITY_LEFT_BIT) == 0) {
		return;
	}

	if (eye == 1 &&
	    (self->visibility & XRT_LAYER_EYE_VISIBILITY_RIGHT_BIT) == 0) {
		return;
	}

	self->vk->vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                            pipeline);

	// Is this layer viewspace or not.
	const struct xrt_matrix_4x4 *vp = self->view_space ? vp_eye : vp_world;

	switch (self->type) {
	case XRT_LAYER_STEREO_PROJECTION:
		_update_mvp_matrix(self, eye, &proj_scale);
		break;
	case XRT_LAYER_QUAD: _update_mvp_matrix(self, eye, vp); break;
	case XRT_LAYER_STEREO_PROJECTION_DEPTH:
	case XRT_LAYER_CUBE:
	case XRT_LAYER_CYLINDER:
	case XRT_LAYER_EQUIRECT:
		// Should never end up here.
		assert(false);
	}

	self->vk->vkCmdBindDescriptorSets(
	    cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
	    &self->descriptor_sets[eye], 0, NULL);

	VkDeviceSize offsets[1] = {0};
	self->vk->vkCmdBindVertexBuffers(cmd_buffer, 0, 1,
	                                 &vertex_buffer->handle, &offsets[0]);

	self->vk->vkCmdDraw(cmd_buffer, vertex_buffer->size, 1, 0, 0);
}

struct comp_render_layer *
comp_layer_create(struct vk_bundle *vk, VkDescriptorSetLayout *layout)
{
	struct comp_render_layer *q = U_TYPED_CALLOC(struct comp_render_layer);

	_init(q, vk, layout);

	return q;
}

void
comp_layer_destroy(struct comp_render_layer *self)
{
	for (uint32_t eye = 0; eye < 2; eye++)
		vk_buffer_destroy(&self->transformation_ubos[eye], self->vk);

	self->vk->vkDestroyDescriptorPool(self->vk->device,
	                                  self->descriptor_pool, NULL);

	free(self);
}
