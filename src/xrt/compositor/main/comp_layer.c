// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor quad rendering.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_main
 */

#include "math/m_mathinclude.h"
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
comp_layer_set_model_matrix(struct comp_render_layer *self, const struct xrt_matrix_4x4 *m)
{
	memcpy(&self->model_matrix, m, sizeof(struct xrt_matrix_4x4));
}

static void
_update_mvp_matrix(struct comp_render_layer *self, uint32_t eye, const struct xrt_matrix_4x4 *vp)
{
	math_matrix_4x4_multiply(vp, &self->model_matrix, &self->transformation[eye].mvp);
	memcpy(self->transformation_ubos[eye].data, &self->transformation[eye], sizeof(struct layer_transformation));
}

static bool
_init_ubos(struct comp_render_layer *self)
{
	VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
	                                   VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

	for (uint32_t i = 0; i < 2; i++) {
		math_matrix_4x4_identity(&self->transformation[i].mvp);

		if (!vk_buffer_init(self->vk, sizeof(struct layer_transformation), usage, properties,
		                    &self->transformation_ubos[i].handle, &self->transformation_ubos[i].memory))
			return false;

		VkResult res = self->vk->vkMapMemory(self->vk->device, self->transformation_ubos[i].memory, 0,
		                                     VK_WHOLE_SIZE, 0, &self->transformation_ubos[i].data);
		vk_check_error("vkMapMemory", res, false);

		memcpy(self->transformation_ubos[i].data, &self->transformation[i],
		       sizeof(struct layer_transformation));
	}
	return true;
}

#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT1
static bool
_init_equirect1_ubo(struct comp_render_layer *self)
{
	VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
	                                   VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

	if (!vk_buffer_init(self->vk, sizeof(struct layer_transformation), usage, properties,
	                    &self->equirect1_ubo.handle, &self->equirect1_ubo.memory))
		return false;

	VkResult res = self->vk->vkMapMemory(self->vk->device, self->equirect1_ubo.memory, 0, VK_WHOLE_SIZE, 0,
	                                     &self->equirect1_ubo.data);
	vk_check_error("vkMapMemory", res, false);

	memcpy(self->equirect1_ubo.data, &self->equirect1_data, sizeof(struct layer_equirect1_data));

	return true;
}
#endif
#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT2
static bool
_init_equirect2_ubo(struct comp_render_layer *self)
{
	VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
	                                   VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

	if (!vk_buffer_init(self->vk, sizeof(struct layer_transformation), usage, properties,
	                    &self->equirect2_ubo.handle, &self->equirect2_ubo.memory))
		return false;

	VkResult res = self->vk->vkMapMemory(self->vk->device, self->equirect2_ubo.memory, 0, VK_WHOLE_SIZE, 0,
	                                     &self->equirect2_ubo.data);
	vk_check_error("vkMapMemory", res, false);

	memcpy(self->equirect2_ubo.data, &self->equirect2_data, sizeof(struct layer_equirect2_data));

	return true;
}
#endif

static void
_update_descriptor(struct comp_render_layer *self,
                   struct vk_bundle *vk,
                   VkDescriptorSet set,
                   VkBuffer transformation_buffer,
                   VkSampler sampler,
                   VkImageView image_view)
{
	VkWriteDescriptorSet *sets = (VkWriteDescriptorSet[]){
	    {
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = set,
	        .dstBinding = self->transformation_ubo_binding,
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
	        .dstBinding = self->texture_binding,
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

#if defined(XRT_FEATURE_OPENXR_LAYER_EQUIRECT1) || defined(XRT_FEATURE_OPENXR_LAYER_EQUIRECT2)
static void
_update_descriptor_equirect(struct comp_render_layer *self, VkDescriptorSet set, VkBuffer buffer)
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
	                .buffer = buffer,
	                .offset = 0,
	                .range = VK_WHOLE_SIZE,
	            },
	        .pTexelBufferView = NULL,
	    },
	};

	self->vk->vkUpdateDescriptorSets(self->vk->device, 1, sets, 0, NULL);
}
#endif

void
comp_layer_update_descriptors(struct comp_render_layer *self, VkSampler sampler, VkImageView image_view)
{
	for (uint32_t eye = 0; eye < 2; eye++)
		_update_descriptor(self, self->vk, self->descriptor_sets[eye], self->transformation_ubos[eye].handle,
		                   sampler, image_view);
}

#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT1
void
comp_layer_update_equirect1_descriptor(struct comp_render_layer *self, struct xrt_layer_equirect1_data *data)
{
	_update_descriptor_equirect(self, self->descriptor_equirect, self->equirect1_ubo.handle);

	self->equirect1_data = (struct layer_equirect1_data){
	    .radius = data->radius,
	    .scale = data->scale,
	    .bias = data->bias,
	};
	memcpy(self->equirect1_ubo.data, &self->equirect1_data, sizeof(struct layer_equirect1_data));
}
#endif
#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT2
void
comp_layer_update_equirect2_descriptor(struct comp_render_layer *self, struct xrt_layer_equirect2_data *data)
{
	_update_descriptor_equirect(self, self->descriptor_equirect, self->equirect2_ubo.handle);

	self->equirect2_data = (struct layer_equirect2_data){
	    .radius = data->radius,
	    .central_horizontal_angle = data->central_horizontal_angle,
	    .upper_vertical_angle = data->upper_vertical_angle,
	    .lower_vertical_angle = data->lower_vertical_angle,
	};
	memcpy(self->equirect2_ubo.data, &self->equirect2_data, sizeof(struct layer_equirect2_data));
}
#endif

void
comp_layer_update_stereo_descriptors(struct comp_render_layer *self,
                                     VkSampler left_sampler,
                                     VkSampler right_sampler,
                                     VkImageView left_image_view,
                                     VkImageView right_image_view)
{
	_update_descriptor(self, self->vk, self->descriptor_sets[0], self->transformation_ubos[0].handle, left_sampler,
	                   left_image_view);

	_update_descriptor(self, self->vk, self->descriptor_sets[1], self->transformation_ubos[1].handle, right_sampler,
	                   right_image_view);
}

static bool
_init(struct comp_render_layer *self,
      struct vk_bundle *vk,
      VkDescriptorSetLayout *layout,
      VkDescriptorSetLayout *layout_equirect)
{
	self->vk = vk;

	self->view_space = true;
	self->visibility = XRT_LAYER_EYE_VISIBILITY_BOTH;

	math_matrix_4x4_identity(&self->model_matrix);

	if (!_init_ubos(self))
		return false;

#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT1
	if (!_init_equirect1_ubo(self))
		return false;
#endif
#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT2
	if (!_init_equirect2_ubo(self))
		return false;
#endif

	VkDescriptorPoolSize pool_sizes[] = {
	    {
	        .descriptorCount = 3,
	        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	    },
	    {
	        .descriptorCount = 2,
	        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	    },
	};

	if (!vk_init_descriptor_pool(self->vk, pool_sizes, ARRAY_SIZE(pool_sizes), 3, &self->descriptor_pool))
		return false;

	for (uint32_t eye = 0; eye < 2; eye++)
		if (!vk_allocate_descriptor_sets(self->vk, self->descriptor_pool, 1, layout,
		                                 &self->descriptor_sets[eye]))
			return false;

#if defined(XRT_FEATURE_OPENXR_LAYER_EQUIRECT1) || defined(XRT_FEATURE_OPENXR_LAYER_EQUIRECT2)
	if (!vk_allocate_descriptor_sets(self->vk, self->descriptor_pool, 1, layout_equirect,
	                                 &self->descriptor_equirect))
		return false;
#endif
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
	if (eye == 0 && (self->visibility & XRT_LAYER_EYE_VISIBILITY_LEFT_BIT) == 0) {
		return;
	}

	if (eye == 1 && (self->visibility & XRT_LAYER_EYE_VISIBILITY_RIGHT_BIT) == 0) {
		return;
	}

	self->vk->vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// Is this layer viewspace or not.
	const struct xrt_matrix_4x4 *vp = self->view_space ? vp_eye : vp_world;

	switch (self->type) {
	case XRT_LAYER_STEREO_PROJECTION: _update_mvp_matrix(self, eye, &proj_scale); break;
	case XRT_LAYER_QUAD:
	case XRT_LAYER_CYLINDER:
	case XRT_LAYER_EQUIRECT1:
	case XRT_LAYER_EQUIRECT2: _update_mvp_matrix(self, eye, vp); break;
	case XRT_LAYER_STEREO_PROJECTION_DEPTH:
	case XRT_LAYER_CUBE:
		// Should never end up here.
		assert(false);
	}


	if (self->type == XRT_LAYER_EQUIRECT1 || self->type == XRT_LAYER_EQUIRECT2) {
		const VkDescriptorSet sets[2] = {
		    self->descriptor_sets[eye],
		    self->descriptor_equirect,
		};

		self->vk->vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 2,
		                                  sets, 0, NULL);

	} else {
		self->vk->vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
		                                  &self->descriptor_sets[eye], 0, NULL);
	}

	VkDeviceSize offsets[1] = {0};
	self->vk->vkCmdBindVertexBuffers(cmd_buffer, 0, 1, &vertex_buffer->handle, &offsets[0]);

	self->vk->vkCmdDraw(cmd_buffer, vertex_buffer->size, 1, 0, 0);
}

// clang-format off
#define CYLINDER_FACES 360
#define CYLINDER_VERTICES CYLINDER_FACES * 6
static float cylinder_vertices[CYLINDER_VERTICES * 5] = {0};
// clang-format on

static void
_calculate_unit_cylinder_segment_vertices(float central_angle)
{
	// unit cylinder with diameter = 1.0, height = 1.0
	double radius = .5;
	double height = 1;
	double angle_offset = M_PI / 2.0;

	double start_angle = central_angle / 2. + angle_offset;
	double angle_step_size = central_angle / (float)CYLINDER_FACES;

	int vertex = 0;
	for (int i = 0; i < CYLINDER_FACES; i++) {
		double t = height / 2.;
		double b = -height / 2.;

		double uv_l = (double)i / (double)(CYLINDER_FACES);
		double uv_r = (double)(i + 1) / (double)(CYLINDER_FACES);
		double uv_t = 1.f;
		double uv_b = 0.f;


		double theta = start_angle - angle_step_size * i;
		double next_theta = start_angle - angle_step_size * (i + 1);

		if (i == CYLINDER_FACES - 1) {
			// remove gap in approximately closed cylinder
			if ((fabs(2 * M_PI - central_angle) < 0.001)) {
				next_theta = start_angle;
			}
			uv_r = 1.0;
		}

		double l = radius * cos(theta);
		double lz = -radius * sin(theta);

		double r = radius * cos(next_theta);
		double rz = -radius * sin(next_theta);

		// cylinder face: quad of 2 triangles
		cylinder_vertices[vertex++] = l;
		cylinder_vertices[vertex++] = b;
		cylinder_vertices[vertex++] = lz;
		cylinder_vertices[vertex++] = uv_l;
		cylinder_vertices[vertex++] = uv_t;

		cylinder_vertices[vertex++] = r;
		cylinder_vertices[vertex++] = b;
		cylinder_vertices[vertex++] = rz;
		cylinder_vertices[vertex++] = uv_r;
		cylinder_vertices[vertex++] = uv_t;

		cylinder_vertices[vertex++] = r;
		cylinder_vertices[vertex++] = t;
		cylinder_vertices[vertex++] = rz;
		cylinder_vertices[vertex++] = uv_r;
		cylinder_vertices[vertex++] = uv_b;

		cylinder_vertices[vertex++] = r;
		cylinder_vertices[vertex++] = t;
		cylinder_vertices[vertex++] = rz;
		cylinder_vertices[vertex++] = uv_r;
		cylinder_vertices[vertex++] = uv_b;

		cylinder_vertices[vertex++] = l;
		cylinder_vertices[vertex++] = t;
		cylinder_vertices[vertex++] = lz;
		cylinder_vertices[vertex++] = uv_l;
		cylinder_vertices[vertex++] = uv_b;

		cylinder_vertices[vertex++] = l;
		cylinder_vertices[vertex++] = b;
		cylinder_vertices[vertex++] = lz;
		cylinder_vertices[vertex++] = uv_l;
		cylinder_vertices[vertex++] = uv_t;
	}
}

bool
comp_layer_update_cylinder_vertex_buffer(struct comp_render_layer *self, float central_angle)
{
	_calculate_unit_cylinder_segment_vertices(central_angle);

	struct vk_bundle *vk = self->vk;
	return vk_update_buffer(vk, cylinder_vertices, sizeof(float) * ARRAY_SIZE(cylinder_vertices),
	                        self->cylinder.vertex_buffer.memory);

	return true;
}

static bool
_init_cylinder_vertex_buffer(struct comp_render_layer *self)
{
	struct vk_bundle *vk = self->vk;

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	if (!vk_buffer_init(vk, sizeof(float) * ARRAY_SIZE(cylinder_vertices), usage, properties,
	                    &self->cylinder.vertex_buffer.handle, &self->cylinder.vertex_buffer.memory))
		return false;

	self->cylinder.vertex_buffer.size = CYLINDER_VERTICES;
	return true;
}

struct vk_buffer *
comp_layer_get_cylinder_vertex_buffer(struct comp_render_layer *self)
{
	return &self->cylinder.vertex_buffer;
}

struct comp_render_layer *
comp_layer_create(struct vk_bundle *vk, VkDescriptorSetLayout *layout, VkDescriptorSetLayout *layout_equirect)
{
	struct comp_render_layer *q = U_TYPED_CALLOC(struct comp_render_layer);

	_init(q, vk, layout, layout_equirect);

	if (!_init_cylinder_vertex_buffer(q))
		return NULL;

	return q;
}

void
comp_layer_destroy(struct comp_render_layer *self)
{
	for (uint32_t eye = 0; eye < 2; eye++)
		vk_buffer_destroy(&self->transformation_ubos[eye], self->vk);

#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT1
	vk_buffer_destroy(&self->equirect1_ubo, self->vk);
#endif
#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT2
	vk_buffer_destroy(&self->equirect2_ubo, self->vk);
#endif
	self->vk->vkDestroyDescriptorPool(self->vk->device, self->descriptor_pool, NULL);

	vk_buffer_destroy(&self->cylinder.vertex_buffer, self->vk);

	free(self);
}
