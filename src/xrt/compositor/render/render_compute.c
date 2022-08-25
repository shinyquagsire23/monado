// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  The compositor compute based rendering code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_render
 */

#include "math/m_api.h"
#include "math/m_matrix_4x4_f64.h"

#include "render/render_interface.h"

#include <stdio.h>


/*
 *
 * Defines
 *
 */

#define C(c)                                                                                                           \
	do {                                                                                                           \
		VkResult ret = c;                                                                                      \
		if (ret != VK_SUCCESS) {                                                                               \
			return false;                                                                                  \
		}                                                                                                      \
	} while (false)

#define D(TYPE, thing)                                                                                                 \
	if (thing != VK_NULL_HANDLE) {                                                                                 \
		vk->vkDestroy##TYPE(vk, vk->device, thing, NULL);                                                      \
		thing = VK_NULL_HANDLE;                                                                                \
	}

#define DD(pool, thing)                                                                                                \
	if (thing != VK_NULL_HANDLE) {                                                                                 \
		free_descriptor_set(vk, pool, thing);                                                                  \
		thing = VK_NULL_HANDLE;                                                                                \
	}


/*
 *
 * Helper functions.
 *
 */

/*!
 * Get the @ref vk_bundle from @ref render_compute.
 */
static inline struct vk_bundle *
vk_from_crc(struct render_compute *crc)
{
	return crc->r->vk;
}

/*
 * For dispatching compute to the view, calculate the number of groups.
 */
static void
calc_dispatch_dims(const struct render_viewport_data views[2], uint32_t *out_w, uint32_t *out_h)
{
#define IMAX(a, b) ((a) > (b) ? (a) : (b))
	uint32_t w = IMAX(views[0].w, views[1].w);
	uint32_t h = IMAX(views[0].h, views[1].h);
#undef IMAX

	// Power of two divide and round up.
#define P2_DIVIDE_ROUND_UP(v, div) ((v + (div - 1)) / div)
	w = P2_DIVIDE_ROUND_UP(w, 8);
	h = P2_DIVIDE_ROUND_UP(h, 8);
#undef P2_DIVIDE_ROUND_UP

	*out_w = w;
	*out_h = h;
}


/*
 *
 * Vulkan helpers.
 *
 */

XRT_MAYBE_UNUSED static void
update_compute_distortion_descriptor_set(struct vk_bundle *vk,
                                         uint32_t src_binding,
                                         VkSampler src_samplers[2],
                                         VkImageView src_image_views[2],
                                         uint32_t distortion_binding,
                                         VkSampler distortion_samplers[6],
                                         VkImageView distortion_image_views[6],
                                         uint32_t target_binding,
                                         VkImageView target_image_view,
                                         uint32_t ubo_binding,
                                         VkBuffer ubo_buffer,
                                         VkDeviceSize ubo_size,
                                         VkDescriptorSet descriptor_set)
{
	VkDescriptorImageInfo src_image_info[2] = {
	    {
	        .sampler = src_samplers[0],
	        .imageView = src_image_views[0],
	        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	    },
	    {
	        .sampler = src_samplers[1],
	        .imageView = src_image_views[1],
	        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	    },
	};

	VkDescriptorImageInfo distortion_image_info[6] = {
	    {
	        .sampler = distortion_samplers[0],
	        .imageView = distortion_image_views[0],
	        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	    },
	    {
	        .sampler = distortion_samplers[1],
	        .imageView = distortion_image_views[1],
	        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	    },
	    {
	        .sampler = distortion_samplers[2],
	        .imageView = distortion_image_views[2],
	        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	    },
	    {
	        .sampler = distortion_samplers[3],
	        .imageView = distortion_image_views[3],
	        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	    },
	    {
	        .sampler = distortion_samplers[4],
	        .imageView = distortion_image_views[4],
	        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	    },
	    {
	        .sampler = distortion_samplers[5],
	        .imageView = distortion_image_views[5],
	        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	    },
	};

	VkDescriptorImageInfo target_image_info = {
	    .imageView = target_image_view,
	    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkDescriptorBufferInfo buffer_info = {
	    .buffer = ubo_buffer,
	    .offset = 0,
	    .range = ubo_size,
	};

	VkWriteDescriptorSet write_descriptor_sets[4] = {
	    {
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = descriptor_set,
	        .dstBinding = src_binding,
	        .descriptorCount = ARRAY_SIZE(src_image_info),
	        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .pImageInfo = src_image_info,
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = descriptor_set,
	        .dstBinding = distortion_binding,
	        .descriptorCount = ARRAY_SIZE(distortion_image_info),
	        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	        .pImageInfo = distortion_image_info,
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = descriptor_set,
	        .dstBinding = target_binding,
	        .descriptorCount = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	        .pImageInfo = &target_image_info,
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = descriptor_set,
	        .dstBinding = ubo_binding,
	        .descriptorCount = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	        .pBufferInfo = &buffer_info,
	    },
	};

	vk->vkUpdateDescriptorSets(            //
	    vk->device,                        //
	    ARRAY_SIZE(write_descriptor_sets), // descriptorWriteCount
	    write_descriptor_sets,             // pDescriptorWrites
	    0,                                 // descriptorCopyCount
	    NULL);                             // pDescriptorCopies
}

XRT_MAYBE_UNUSED static void
update_compute_discriptor_set_target(struct vk_bundle *vk,
                                     uint32_t target_binding,
                                     VkImageView target_image_view,
                                     uint32_t ubo_binding,
                                     VkBuffer ubo_buffer,
                                     VkDeviceSize ubo_size,
                                     VkDescriptorSet descriptor_set)
{
	VkDescriptorImageInfo target_image_info = {
	    .imageView = target_image_view,
	    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkDescriptorBufferInfo buffer_info = {
	    .buffer = ubo_buffer,
	    .offset = 0,
	    .range = ubo_size,
	};

	VkWriteDescriptorSet write_descriptor_sets[2] = {
	    {
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = descriptor_set,
	        .dstBinding = target_binding,
	        .descriptorCount = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	        .pImageInfo = &target_image_info,
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	        .dstSet = descriptor_set,
	        .dstBinding = ubo_binding,
	        .descriptorCount = 1,
	        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	        .pBufferInfo = &buffer_info,
	    },
	};

	vk->vkUpdateDescriptorSets(            //
	    vk->device,                        //
	    ARRAY_SIZE(write_descriptor_sets), // descriptorWriteCount
	    write_descriptor_sets,             // pDescriptorWrites
	    0,                                 // descriptorCopyCount
	    NULL);                             // pDescriptorCopies
}


/*
 *
 * 'Exported' functions.
 *
 */

bool
render_compute_init(struct render_compute *crc, struct render_resources *r)
{
	assert(crc->r == NULL);

	struct vk_bundle *vk = r->vk;
	crc->r = r;

	C(vk_create_descriptor_set(                      //
	    vk,                                          //
	    r->compute.descriptor_pool,                  // descriptor_pool
	    r->compute.distortion.descriptor_set_layout, // descriptor_set_layout
	    &crc->distortion_descriptor_set));           // descriptor_set

	return true;
}

bool
render_compute_begin(struct render_compute *crc)
{
	struct vk_bundle *vk = vk_from_crc(crc);

	C(vk->vkResetCommandPool(vk->device, crc->r->cmd_pool, 0));

	VkCommandBufferBeginInfo begin_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	C(vk->vkBeginCommandBuffer( //
	    crc->r->cmd,            // commandBuffer
	    &begin_info));          // pBeginInfo

	vk->vkCmdResetQueryPool( //
	    crc->r->cmd,         // commandBuffer
	    crc->r->query_pool,  // queryPool
	    0,                   // firstQuery
	    2);                  // queryCount

	vk->vkCmdWriteTimestamp(               //
	    crc->r->cmd,                       // commandBuffer
	    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, // pipelineStage
	    crc->r->query_pool,                // queryPool
	    0);                                // query

	return true;
}

bool
render_compute_end(struct render_compute *crc)
{
	struct vk_bundle *vk = vk_from_crc(crc);

	vk->vkCmdWriteTimestamp(                  //
	    crc->r->cmd,                          // commandBuffer
	    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, // pipelineStage
	    crc->r->query_pool,                   // queryPool
	    1);                                   // query

	C(vk->vkEndCommandBuffer(crc->r->cmd));

	return true;
}

void
render_compute_close(struct render_compute *crc)
{
	assert(crc->r != NULL);

	struct vk_bundle *vk = vk_from_crc(crc);

	// Reclaimed by vkResetDescriptorPool.
	crc->distortion_descriptor_set = VK_NULL_HANDLE;

	vk->vkResetDescriptorPool(vk->device, crc->r->compute.descriptor_pool, 0);

	crc->r = NULL;
}

void
render_compute_projection_timewarp(struct render_compute *crc,
                                   VkSampler src_samplers[2],
                                   VkImageView src_image_views[2],
                                   const struct xrt_normalized_rect src_norm_rects[2],
                                   const struct xrt_pose src_poses[2],
                                   const struct xrt_fov src_fovs[2],
                                   const struct xrt_pose new_poses[2],
                                   VkImage target_image,
                                   VkImageView target_image_view,
                                   const struct render_viewport_data views[2])
{
	assert(crc->r != NULL);

	struct vk_bundle *vk = vk_from_crc(crc);
	struct render_resources *r = crc->r;


	/*
	 * UBO
	 */

	struct xrt_matrix_4x4 time_warp_matrix[2];
	render_calc_time_warp_matrix( //
	    &src_poses[0],            //
	    &src_fovs[0],             //
	    &new_poses[0],            //
	    &time_warp_matrix[0]);    //
	render_calc_time_warp_matrix( //
	    &src_poses[1],            //
	    &src_fovs[1],             //
	    &new_poses[1],            //
	    &time_warp_matrix[1]);    //

	struct render_compute_distortion_ubo_data *data =
	    (struct render_compute_distortion_ubo_data *)r->compute.distortion.ubo.mapped;
	data->views[0] = views[0];
	data->views[1] = views[1];
	data->pre_transforms[0] = r->distortion.uv_to_tanangle[0];
	data->pre_transforms[1] = r->distortion.uv_to_tanangle[1];
	data->transforms[0] = time_warp_matrix[0];
	data->transforms[1] = time_warp_matrix[1];
	data->post_transforms[0] = src_norm_rects[0];
	data->post_transforms[1] = src_norm_rects[1];


	/*
	 * Source, target and distortion images.
	 */

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = VK_REMAINING_MIP_LEVELS,
	    .baseArrayLayer = 0,
	    .layerCount = VK_REMAINING_ARRAY_LAYERS,
	};

	vk_cmd_image_barrier_gpu_locked( //
	    vk,                          //
	    r->cmd,                      //
	    target_image,                //
	    0,                           //
	    VK_ACCESS_SHADER_WRITE_BIT,  //
	    VK_IMAGE_LAYOUT_UNDEFINED,   //
	    VK_IMAGE_LAYOUT_GENERAL,     //
	    subresource_range);          //

	VkSampler sampler = r->compute.default_sampler;
	VkSampler distortion_samplers[6] = {
	    sampler, sampler, sampler, sampler, sampler, sampler,
	};

	update_compute_distortion_descriptor_set( //
	    vk,                                   //
	    r->compute.src_binding,               //
	    src_samplers,                         //
	    src_image_views,                      //
	    r->compute.distortion_binding,        //
	    distortion_samplers,                  //
	    r->distortion.image_views,            //
	    r->compute.target_binding,            //
	    target_image_view,                    //
	    r->compute.ubo_binding,               //
	    r->compute.distortion.ubo.buffer,     //
	    VK_WHOLE_SIZE,                        //
	    crc->distortion_descriptor_set);      //

	vk->vkCmdBindPipeline(                        //
	    r->cmd,                                   // commandBuffer
	    VK_PIPELINE_BIND_POINT_COMPUTE,           // pipelineBindPoint
	    r->compute.distortion.timewarp_pipeline); // pipeline

	vk->vkCmdBindDescriptorSets(               //
	    r->cmd,                                // commandBuffer
	    VK_PIPELINE_BIND_POINT_COMPUTE,        // pipelineBindPoint
	    r->compute.distortion.pipeline_layout, // layout
	    0,                                     // firstSet
	    1,                                     // descriptorSetCount
	    &crc->distortion_descriptor_set,       // pDescriptorSets
	    0,                                     // dynamicOffsetCount
	    NULL);                                 // pDynamicOffsets


	uint32_t w = 0, h = 0;
	calc_dispatch_dims(views, &w, &h);
	assert(w != 0 && h != 0);

	vk->vkCmdDispatch( //
	    r->cmd,        // commandBuffer
	    w,             // groupCountX
	    h,             // groupCountY
	    2);            // groupCountZ

	VkImageMemoryBarrier memoryBarrier = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
	    .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
	    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
	    .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .image = target_image,
	    .subresourceRange = subresource_range,
	};

	vk->vkCmdPipelineBarrier(                 //
	    r->cmd,                               //
	    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, //
	    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,    //
	    0,                                    //
	    0,                                    //
	    NULL,                                 //
	    0,                                    //
	    NULL,                                 //
	    1,                                    //
	    &memoryBarrier);                      //
}

void
render_compute_projection(struct render_compute *crc,
                          VkSampler src_samplers[2],
                          VkImageView src_image_views[2],
                          const struct xrt_normalized_rect src_norm_rects[2],
                          VkImage target_image,
                          VkImageView target_image_view,
                          const struct render_viewport_data views[2])
{
	assert(crc->r != NULL);

	struct vk_bundle *vk = vk_from_crc(crc);
	struct render_resources *r = crc->r;


	/*
	 * UBO
	 */

	struct render_compute_distortion_ubo_data *data =
	    (struct render_compute_distortion_ubo_data *)r->compute.distortion.ubo.mapped;
	data->views[0] = views[0];
	data->views[1] = views[1];
	data->post_transforms[0] = src_norm_rects[0];
	data->post_transforms[1] = src_norm_rects[1];


	/*
	 * Source, target and distortion images.
	 */

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = VK_REMAINING_MIP_LEVELS,
	    .baseArrayLayer = 0,
	    .layerCount = VK_REMAINING_ARRAY_LAYERS,
	};

	vk_cmd_image_barrier_gpu_locked( //
	    vk,                          //
	    r->cmd,                      //
	    target_image,                //
	    0,                           //
	    VK_ACCESS_SHADER_WRITE_BIT,  //
	    VK_IMAGE_LAYOUT_UNDEFINED,   //
	    VK_IMAGE_LAYOUT_GENERAL,     //
	    subresource_range);          //

	VkSampler sampler = r->compute.default_sampler;
	VkSampler distortion_samplers[6] = {
	    sampler, sampler, sampler, sampler, sampler, sampler,
	};

	update_compute_distortion_descriptor_set( //
	    vk,                                   //
	    r->compute.src_binding,               //
	    src_samplers,                         //
	    src_image_views,                      //
	    r->compute.distortion_binding,        //
	    distortion_samplers,                  //
	    r->distortion.image_views,            //
	    r->compute.target_binding,            //
	    target_image_view,                    //
	    r->compute.ubo_binding,               //
	    r->compute.distortion.ubo.buffer,     //
	    VK_WHOLE_SIZE,                        //
	    crc->distortion_descriptor_set);      //

	vk->vkCmdBindPipeline(               //
	    r->cmd,                          // commandBuffer
	    VK_PIPELINE_BIND_POINT_COMPUTE,  // pipelineBindPoint
	    r->compute.distortion.pipeline); // pipeline

	vk->vkCmdBindDescriptorSets(               //
	    r->cmd,                                // commandBuffer
	    VK_PIPELINE_BIND_POINT_COMPUTE,        // pipelineBindPoint
	    r->compute.distortion.pipeline_layout, // layout
	    0,                                     // firstSet
	    1,                                     // descriptorSetCount
	    &crc->distortion_descriptor_set,       // pDescriptorSets
	    0,                                     // dynamicOffsetCount
	    NULL);                                 // pDynamicOffsets


	uint32_t w = 0, h = 0;
	calc_dispatch_dims(views, &w, &h);
	assert(w != 0 && h != 0);

	vk->vkCmdDispatch( //
	    r->cmd,        // commandBuffer
	    w,             // groupCountX
	    h,             // groupCountY
	    2);            // groupCountZ

	VkImageMemoryBarrier memoryBarrier = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
	    .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
	    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
	    .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .image = target_image,
	    .subresourceRange = subresource_range,
	};

	vk->vkCmdPipelineBarrier(                 //
	    r->cmd,                               //
	    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, //
	    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,    //
	    0,                                    //
	    0,                                    //
	    NULL,                                 //
	    0,                                    //
	    NULL,                                 //
	    1,                                    //
	    &memoryBarrier);                      //
}

void
render_compute_clear(struct render_compute *crc,                 //
                     VkImage target_image,                       //
                     VkImageView target_image_view,              //
                     const struct render_viewport_data views[2]) //
{
	assert(crc->r != NULL);

	struct vk_bundle *vk = vk_from_crc(crc);
	struct render_resources *r = crc->r;


	/*
	 * UBO
	 */

	// Calculate transforms.
	struct xrt_matrix_4x4 transforms[2];
	for (uint32_t i = 0; i < 2; i++) {
		math_matrix_4x4_identity(&transforms[i]);
	}

	struct render_compute_distortion_ubo_data *data =
	    (struct render_compute_distortion_ubo_data *)r->compute.clear.ubo.mapped;
	data->views[0] = views[0];
	data->views[1] = views[1];


	/*
	 * Source, target and distortion images.
	 */

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = VK_REMAINING_MIP_LEVELS,
	    .baseArrayLayer = 0,
	    .layerCount = VK_REMAINING_ARRAY_LAYERS,
	};

	vk_cmd_image_barrier_gpu_locked( //
	    vk,                          //
	    r->cmd,                      //
	    target_image,                //
	    0,                           //
	    VK_ACCESS_SHADER_WRITE_BIT,  //
	    VK_IMAGE_LAYOUT_UNDEFINED,   //
	    VK_IMAGE_LAYOUT_GENERAL,     //
	    subresource_range);          //

	VkSampler sampler = r->compute.default_sampler;
	VkSampler src_samplers[2] = {sampler, sampler};
	VkImageView src_image_views[2] = {r->mock.color.image_view, r->mock.color.image_view};
	VkSampler distortion_samplers[6] = {sampler, sampler, sampler, sampler, sampler, sampler};

	update_compute_distortion_descriptor_set( //
	    vk,                                   // vk_bundle
	    r->compute.src_binding,               // src_binding
	    src_samplers,                         // src_samplers[2]
	    src_image_views,                      // src_image_views[2]
	    r->compute.distortion_binding,        // distortion_binding
	    distortion_samplers,                  // distortion_samplers[6]
	    r->distortion.image_views,            // distortion_image_views[6]
	    r->compute.target_binding,            // target_binding
	    target_image_view,                    // target_image_view
	    r->compute.ubo_binding,               // ubo_binding
	    r->compute.clear.ubo.buffer,          // ubo_buffer
	    VK_WHOLE_SIZE,                        // ubo_size
	    crc->distortion_descriptor_set);      // descriptor_set

	vk->vkCmdBindPipeline(              //
	    r->cmd,                         // commandBuffer
	    VK_PIPELINE_BIND_POINT_COMPUTE, // pipelineBindPoint
	    r->compute.clear.pipeline);     // pipeline

	vk->vkCmdBindDescriptorSets(               //
	    r->cmd,                                // commandBuffer
	    VK_PIPELINE_BIND_POINT_COMPUTE,        // pipelineBindPoint
	    r->compute.distortion.pipeline_layout, // layout
	    0,                                     // firstSet
	    1,                                     // descriptorSetCount
	    &crc->distortion_descriptor_set,       // pDescriptorSets
	    0,                                     // dynamicOffsetCount
	    NULL);                                 // pDynamicOffsets


	uint32_t w = 0, h = 0;
	calc_dispatch_dims(views, &w, &h);
	assert(w != 0 && h != 0);

	vk->vkCmdDispatch( //
	    r->cmd,        // commandBuffer
	    w,             // groupCountX
	    h,             // groupCountY
	    2);            // groupCountZ

	VkImageMemoryBarrier memoryBarrier = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
	    .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
	    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
	    .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .image = target_image,
	    .subresourceRange = subresource_range,
	};

	vk->vkCmdPipelineBarrier(                 //
	    r->cmd,                               //
	    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, //
	    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,    //
	    0,                                    //
	    0,                                    //
	    NULL,                                 //
	    0,                                    //
	    NULL,                                 //
	    1,                                    //
	    &memoryBarrier);                      //
}
