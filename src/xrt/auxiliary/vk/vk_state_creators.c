// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan state creators helpers.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup aux_vk
 */

#include "vk/vk_helpers.h"


VkResult
vk_create_descriptor_pool(struct vk_bundle *vk,
                          const struct vk_descriptor_pool_info *info,
                          VkDescriptorPool *out_descriptor_pool)
{
	VkResult ret;

	uint32_t pool_count = 0;
	VkDescriptorPoolSize pool_sizes[4] = {0};

	const uint32_t descriptor_count = info->descriptor_count;
	const uint32_t uniform_count = info->uniform_per_descriptor_count * descriptor_count;
	const uint32_t sampler_count = info->sampler_per_descriptor_count * descriptor_count;
	const uint32_t storage_image_count = info->storage_image_per_descriptor_count * descriptor_count;
	const uint32_t storage_buffer_count = info->storage_buffer_per_descriptor_count * descriptor_count;

	if (uniform_count > 0) {
		pool_sizes[pool_count].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		pool_sizes[pool_count].descriptorCount = uniform_count;
		pool_count++;
	}

	if (sampler_count > 0) {
		pool_sizes[pool_count].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		pool_sizes[pool_count].descriptorCount = sampler_count;
		pool_count++;
	}

	if (storage_image_count > 0) {
		pool_sizes[pool_count].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		pool_sizes[pool_count].descriptorCount = storage_image_count;
		pool_count++;
	}

	if (storage_buffer_count > 0) {
		pool_sizes[pool_count].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		pool_sizes[pool_count].descriptorCount = storage_buffer_count;
		pool_count++;
	}

	assert(pool_count > 0 && pool_count <= ARRAY_SIZE(pool_sizes));

	VkDescriptorPoolCreateFlags flags = 0;

	if (info->freeable) {
		flags |= VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	}

	VkDescriptorPoolCreateInfo descriptor_pool_info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	    .flags = flags,
	    .maxSets = descriptor_count,
	    .poolSizeCount = pool_count,
	    .pPoolSizes = pool_sizes,
	};

	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	ret = vk->vkCreateDescriptorPool( //
	    vk->device,                   // device
	    &descriptor_pool_info,        // pCreateInfo
	    NULL,                         // pAllocator
	    &descriptor_pool);            // pDescriptorPool
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateRenderPass failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_descriptor_pool = descriptor_pool;

	return VK_SUCCESS;
}

VkResult
vk_create_descriptor_set(struct vk_bundle *vk,
                         VkDescriptorPool descriptor_pool,
                         VkDescriptorSetLayout descriptor_layout,
                         VkDescriptorSet *out_descriptor_set)
{
	VkResult ret;

	VkDescriptorSetAllocateInfo alloc_info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
	    .descriptorPool = descriptor_pool,
	    .descriptorSetCount = 1,
	    .pSetLayouts = &descriptor_layout,
	};

	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	ret = vk->vkAllocateDescriptorSets( //
	    vk->device,                     // device
	    &alloc_info,                    // pAllocateInfo
	    &descriptor_set);               // pDescriptorSets
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "vkAllocateDescriptorSets failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_descriptor_set = descriptor_set;

	return VK_SUCCESS;
}

VkResult
vk_create_pipeline_layout(struct vk_bundle *vk,
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
	ret = vk->vkCreatePipelineLayout( //
	    vk->device,                   // device
	    &pipeline_layout_info,        // pCreateInfo
	    NULL,                         // pAllocator
	    &pipeline_layout);            // pPipelineLayout
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreatePipelineLayout failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_pipeline_layout = pipeline_layout;

	return VK_SUCCESS;
}

VkResult
vk_create_pipeline_cache(struct vk_bundle *vk, VkPipelineCache *out_pipeline_cache)
{
	VkResult ret;

	VkPipelineCacheCreateInfo pipeline_cache_info = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
	};

	VkPipelineCache pipeline_cache;
	ret = vk->vkCreatePipelineCache( //
	    vk->device,                  // device
	    &pipeline_cache_info,        // pCreateInfo
	    NULL,                        // pAllocator
	    &pipeline_cache);            // pPipelineCache
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreatePipelineCache failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_pipeline_cache = pipeline_cache;

	return VK_SUCCESS;
}

VkResult
vk_create_compute_pipeline(struct vk_bundle *vk,
                           VkPipelineCache pipeline_cache,
                           VkShaderModule shader,
                           VkPipelineLayout pipeline_layout,
                           const VkSpecializationInfo *specialization_info,
                           VkPipeline *out_compute_pipeline)
{
	VkResult ret;

	VkPipelineShaderStageCreateInfo shader_stage_info = {
	    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
	    .pNext = NULL,
	    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
	    .module = shader,
	    .pName = "main",
	    .pSpecializationInfo = specialization_info,
	};

	VkComputePipelineCreateInfo pipeline_info = {
	    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
	    .pNext = NULL,
	    .flags = 0,
	    .stage = shader_stage_info,
	    .layout = pipeline_layout,
	};

	VkPipeline pipeline = VK_NULL_HANDLE;
	ret = vk->vkCreateComputePipelines( //
	    vk->device,                     // device
	    pipeline_cache,                 // pipelineCache
	    1,                              // createInfoCount
	    &pipeline_info,                 // pCreateInfos
	    NULL,                           // pAllocator
	    &pipeline);                     // pPipelines
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "vkCreateComputePipelines failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_compute_pipeline = pipeline;

	return VK_SUCCESS;
}
