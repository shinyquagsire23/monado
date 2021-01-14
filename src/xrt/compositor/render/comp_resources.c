// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared resources for rendering.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */


#include "main/comp_compositor.h"
#include "render/comp_render.h"

#include <stdio.h>


#define C(c)                                                                                                           \
	do {                                                                                                           \
		VkResult ret = c;                                                                                      \
		if (ret != VK_SUCCESS) {                                                                               \
			return false;                                                                                  \
		}                                                                                                      \
	} while (false)

#define D(TYPE, thing)                                                                                                 \
	if (thing != VK_NULL_HANDLE) {                                                                                 \
		vk->vkDestroy##TYPE(vk->device, thing, NULL);                                                          \
		thing = VK_NULL_HANDLE;                                                                                \
	}

static VkResult
create_pipeline_cache(struct vk_bundle *vk, VkPipelineCache *out_pipeline_cache)
{
	VkResult ret;

	VkPipelineCacheCreateInfo pipeline_cache_info = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
	};

	VkPipelineCache pipeline_cache;
	ret = vk->vkCreatePipelineCache(vk->device,           //
	                                &pipeline_cache_info, //
	                                NULL,                 //
	                                &pipeline_cache);     //
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreatePipelineCache failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_pipeline_cache = pipeline_cache;

	return VK_SUCCESS;
}

static VkResult
create_pipeline_layout(struct vk_bundle *vk,
                       VkDescriptorSetLayout descriptor_set_layout,
                       VkPipelineLayout *out_pipeline_layout)
{
	VkResult ret;

	VkPipelineLayoutCreateInfo pipeline_layout_info = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	    .setLayoutCount = 1,
	    .pSetLayouts = &descriptor_set_layout,
	};

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	ret = vk->vkCreatePipelineLayout(vk->device,            //
	                                 &pipeline_layout_info, //
	                                 NULL,                  //
	                                 &pipeline_layout);     //
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreatePipelineLayout failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_pipeline_layout = pipeline_layout;

	return VK_SUCCESS;
}

static VkResult
create_descriptor_pool(struct vk_bundle *vk,
                       uint32_t num_uniform_per_desc,
                       uint32_t num_sampler_per_desc,
                       uint32_t num_descs,
                       VkDescriptorPool *out_descriptor_pool)
{
	VkResult ret;


	VkDescriptorPoolSize pool_sizes[2] = {
	    {
	        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	        .descriptorCount = num_uniform_per_desc * num_descs,
	    },
	    {
	        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .descriptorCount = num_sampler_per_desc * num_descs,
	    },
	};

	VkDescriptorPoolCreateInfo descriptor_pool_info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	    .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
	    .maxSets = num_descs,
	    .poolSizeCount = ARRAY_SIZE(pool_sizes),
	    .pPoolSizes = pool_sizes,
	};

	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	ret = vk->vkCreateDescriptorPool(vk->device,            //
	                                 &descriptor_pool_info, //
	                                 NULL,                  //
	                                 &descriptor_pool);     //
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateRenderPass failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_descriptor_pool = descriptor_pool;

	return VK_SUCCESS;
}


/*
 *
 * Mesh
 *
 */

static VkResult
create_mesh_descriptor_set_layout(struct vk_bundle *vk,
                                  uint32_t src_binding,
                                  uint32_t ubo_binding,
                                  VkDescriptorSetLayout *out_descriptor_set_layout)
{
	VkResult ret;

	VkDescriptorSetLayoutBinding set_layout_bindings[2] = {
	    {
	        .binding = src_binding,
	        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .descriptorCount = 1,
	        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	    },
	    {
	        .binding = ubo_binding,
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

	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	ret = vk->vkCreateDescriptorSetLayout(vk->device,              //
	                                      &set_layout_info,        //
	                                      NULL,                    //
	                                      &descriptor_set_layout); //
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateDescriptorSetLayout failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_descriptor_set_layout = descriptor_set_layout;

	return VK_SUCCESS;
}


static bool
init_mesh_vertex_buffers(struct vk_bundle *vk,
                         struct comp_buffer *vbo,
                         struct comp_buffer *ibo,
                         uint32_t num_vertices,
                         uint32_t stride,
                         void *vertices,
                         uint32_t num_indices,
                         void *indices)
{
	// Using the same flags for all vbos.
	VkBufferUsageFlags vbo_usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkBufferUsageFlags ibo_usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	VkMemoryPropertyFlags memory_property_flags =
	    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

	// Distortion vbo and ibo sizes.
	VkDeviceSize vbo_size = stride * num_vertices;
	VkDeviceSize ibo_size = sizeof(int) * num_indices;


	// Don't create vbo if size is zero.
	if (vbo_size == 0) {
		return true;
	}

	C(comp_buffer_init(vk,                    // vk_bundle
	                   vbo,                   // buffer
	                   vbo_usage_flags,       // usage_flags
	                   memory_property_flags, // memory_property_flags
	                   vbo_size));            // size

	C(comp_buffer_write(vk,         // vk_bundle
	                    vbo,        // buffer
	                    vertices,   // data
	                    vbo_size)); // size


	// Don't create index buffer if size is zero.
	if (ibo_size == 0) {
		return true;
	}

	C(comp_buffer_init(vk,                    // vk_bundle
	                   ibo,                   // buffer
	                   ibo_usage_flags,       // usage_flags
	                   memory_property_flags, // memory_property_flags
	                   ibo_size));            // size

	C(comp_buffer_write(vk,         // vk_bundle
	                    ibo,        // buffer
	                    indices,    // data
	                    ibo_size)); // size

	return true;
}


/*
 *
 * 'Exported' renderer functions.
 *
 */

bool
comp_resources_init(struct comp_compositor *c, struct comp_resources *r)
{
	struct vk_bundle *vk = &c->vk;
	struct xrt_device *xdev = c->xdev;

	/*
	 * Constants
	 */

	r->mesh.src_binding = 0;
	r->mesh.ubo_binding = 1;
	struct xrt_hmd_parts *parts = xdev->hmd;
	r->mesh.num_vertices = parts->distortion.mesh.num_vertices;
	r->mesh.stride = parts->distortion.mesh.stride;
	r->mesh.num_indices[0] = parts->distortion.mesh.num_indices[0];
	r->mesh.num_indices[1] = parts->distortion.mesh.num_indices[1];
	r->mesh.total_num_indices = parts->distortion.mesh.total_num_indices;
	r->mesh.offset_indices[0] = parts->distortion.mesh.offset_indices[0];
	r->mesh.offset_indices[1] = parts->distortion.mesh.offset_indices[1];


	/*
	 * Shared
	 */

	C(create_pipeline_cache(vk, &r->pipeline_cache));


	/*
	 * Mesh static.
	 */

	C(create_descriptor_pool(vk,                         // vk_bundle
	                         1,                          // num_uniform_per_desc
	                         1,                          // num_sampler_per_desc
	                         16 * 2,                     // num_descs
	                         &r->mesh_descriptor_pool)); // out_descriptor_pool

	C(create_mesh_descriptor_set_layout(vk,                               // vk_bundle
	                                    r->mesh.src_binding,              // src_binding
	                                    r->mesh.ubo_binding,              // ubo_binding
	                                    &r->mesh.descriptor_set_layout)); // out_mesh_descriptor_set_layout

	C(create_pipeline_layout(vk,                            // vk_bundle
	                         r->mesh.descriptor_set_layout, // descriptor_set_layout
	                         &r->mesh.pipeline_layout));    // out_pipeline_layout

	if (!init_mesh_vertex_buffers(vk,                                //
	                              &r->mesh.vbo,                      //
	                              &r->mesh.ibo,                      //
	                              r->mesh.num_vertices,              //
	                              r->mesh.stride,                    //
	                              parts->distortion.mesh.vertices,   //
	                              r->mesh.total_num_indices,         //
	                              parts->distortion.mesh.indices)) { //
		return false;
	}


	/*
	 * Done
	 */

	U_LOG_I("New renderer initialized!");

	return true;
}

void
comp_resources_close(struct comp_compositor *c, struct comp_resources *r)
{
	struct vk_bundle *vk = &c->vk;

	D(DescriptorSetLayout, r->mesh.descriptor_set_layout);
	D(PipelineLayout, r->mesh.pipeline_layout);
	D(PipelineCache, r->pipeline_cache);
	D(DescriptorPool, r->mesh_descriptor_pool);
	comp_buffer_close(vk, &r->mesh.vbo);
	comp_buffer_close(vk, &r->mesh.ibo);
}
