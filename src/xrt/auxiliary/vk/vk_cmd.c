// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Command pool helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup aux_vk
 */

#include "vk/vk_cmd.h"


XRT_CHECK_RESULT VkResult
vk_cmd_create_cmd_buffer_locked(struct vk_bundle *vk, VkCommandPool pool, VkCommandBuffer *out_cmd_buffer)
{
	VkCommandBuffer cmd_buffer;
	VkResult ret;

	// Allocate the command buffer.
	VkCommandBufferAllocateInfo cmd_buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	    .commandPool = pool,
	    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	    .commandBufferCount = 1,
	};

	ret = vk->vkAllocateCommandBuffers(vk->device, &cmd_buffer_info, &cmd_buffer);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkAllocateCommandBuffers: %s", vk_result_string(ret));
		// Nothing to cleanup
		return ret;
	}

	*out_cmd_buffer = cmd_buffer;

	return VK_SUCCESS;
}

XRT_CHECK_RESULT VkResult
vk_cmd_create_and_begin_cmd_buffer_locked(struct vk_bundle *vk,
                                          VkCommandPool pool,
                                          VkCommandBufferUsageFlags flags,
                                          VkCommandBuffer *out_cmd_buffer)
{
	VkCommandBuffer cmd_buffer;
	VkResult ret;

	ret = vk_cmd_create_cmd_buffer_locked(vk, pool, &cmd_buffer);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vk_cmd_create_cmd_buffer_locked: %s", vk_result_string(ret));
		// Nothing to cleanup
		return ret;
	}

	// Start the command buffer as well.
	VkCommandBufferBeginInfo begin_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	    .flags = flags,
	};

	ret = vk->vkBeginCommandBuffer(cmd_buffer, &begin_info);

	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkBeginCommandBuffer: %s", vk_result_string(ret));
		goto err_buffer;
	}

	*out_cmd_buffer = cmd_buffer;

	return VK_SUCCESS;


err_buffer:
	vk->vkFreeCommandBuffers(vk->device, pool, 1, &cmd_buffer);

	return ret;
}

XRT_CHECK_RESULT VkResult
vk_cmd_submit_locked(struct vk_bundle *vk, uint32_t count, const VkSubmitInfo *infos, VkFence fence)
{
	VkResult ret;

	os_mutex_lock(&vk->queue_mutex);
	ret = vk->vkQueueSubmit(vk->queue, count, infos, fence);
	os_mutex_unlock(&vk->queue_mutex);

	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkQueueSubmit: %s", vk_result_string(ret));
	}

	return ret;
}

XRT_CHECK_RESULT VkResult
vk_cmd_end_submit_wait_and_free_cmd_buffer_locked(struct vk_bundle *vk, VkCommandPool pool, VkCommandBuffer cmd_buffer)
{
	VkFence fence;
	VkResult ret;

	// Finish the command buffer first, the command buffer pool lock needs to be held.
	ret = vk->vkEndCommandBuffer(cmd_buffer);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkEndCommandBuffer: %s", vk_result_string(ret));
		goto out;
	}

	// Create the fence.
	VkFenceCreateInfo fence_info = {
	    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	};

	ret = vk->vkCreateFence(vk->device, &fence_info, NULL, &fence);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateFence: %s", vk_result_string(ret));
		goto out;
	}

	// Do the submit.
	VkSubmitInfo submitInfo = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &cmd_buffer,
	};

	ret = vk_cmd_submit_locked(vk, 1, &submitInfo, fence);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vk_cmd_pool_submit_locked: %s", vk_result_string(ret));
		goto out_fence;
	}

	// Then wait for the fence.
	ret = vk->vkWaitForFences(vk->device, 1, &fence, VK_TRUE, 1000000000);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkWaitForFences: %s", vk_result_string(ret));
		goto out_fence;
	}

	// Yes fall through.

out_fence:
	vk->vkDestroyFence(vk->device, fence, NULL);
out:
	// Destroy the command buffer, the command buffer pool lock needs to be held.
	vk->vkFreeCommandBuffers(vk->device, pool, 1, &cmd_buffer);

	return ret;
}


/*
 *
 * Command writing functions.
 *
 */

void
vk_cmd_copy_image_locked(struct vk_bundle *vk, VkCommandBuffer cmd_buffer, const struct vk_cmd_copy_image_info *info)
{
	VkPipelineStageFlags src_stage_mask = 0;
	src_stage_mask |= info->src.src_stage_mask;
	src_stage_mask |= info->dst.src_stage_mask;

	VkImageMemoryBarrier barriers[2] = {
	    {
	        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	        .srcAccessMask = info->src.src_access_mask,
	        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
	        .oldLayout = info->src.old_layout,
	        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	        .image = info->src.fm_image.image,
	        .subresourceRange =
	            {
	                .aspectMask = info->src.fm_image.aspect_mask,
	                .baseMipLevel = 0,
	                .levelCount = 1,
	                .baseArrayLayer = info->src.fm_image.base_array_layer,
	                .layerCount = 1,
	            },
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	        .srcAccessMask = info->dst.src_access_mask,
	        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
	        .oldLayout = info->dst.old_layout,
	        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	        .image = info->dst.fm_image.image,
	        .subresourceRange =
	            {
	                .aspectMask = info->dst.fm_image.aspect_mask,
	                .baseMipLevel = 0,
	                .levelCount = 1,
	                .baseArrayLayer = info->dst.fm_image.base_array_layer,
	                .layerCount = 1,
	            },
	    },
	};

	vk->vkCmdPipelineBarrier(           //
	    cmd_buffer,                     // commandBuffer
	    src_stage_mask,                 // srcStageMask
	    VK_PIPELINE_STAGE_TRANSFER_BIT, // dstStageMask
	    0,                              // dependencyFlags
	    0,                              // memoryBarrierCount
	    NULL,                           // pMemoryBarriers
	    0,                              // bufferMemoryBarrierCount
	    NULL,                           // pBufferMemoryBarriers
	    ARRAY_SIZE(barriers),           // imageMemoryBarrierCount
	    barriers);                      // pImageMemoryBarriers

	// Specify the source region to copy from.
	VkImageSubresourceLayers src_subresource = {
	    .aspectMask = info->src.fm_image.aspect_mask,
	    .mipLevel = 0,
	    .baseArrayLayer = info->src.fm_image.base_array_layer,
	    .layerCount = 1,
	};

	// Specify the destination region to copy to.
	VkImageSubresourceLayers dst_subresource = {
	    .aspectMask = info->dst.fm_image.aspect_mask,
	    .mipLevel = 0,
	    .baseArrayLayer = info->dst.fm_image.base_array_layer,
	    .layerCount = 1,
	};

	// Region to copy.
	VkImageCopy copy_region = {
	    .extent =
	        {
	            .width = info->size.w,  // Width of the region to copy.
	            .height = info->size.h, // Height of the region to copy.
	            .depth = 1,
	        },
	    .srcSubresource = src_subresource,
	    .srcOffset = {0, 0, 0},
	    .dstSubresource = dst_subresource,
	    .dstOffset = {0, 0, 0},
	};

	vk->vkCmdCopyImage(                       //
	    cmd_buffer,                           //
	    info->src.fm_image.image,             //
	    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, //
	    info->dst.fm_image.image,             //
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, //
	    1,                                    //
	    &copy_region);                        //
}

void
vk_cmd_blit_image_locked(struct vk_bundle *vk, VkCommandBuffer cmd_buffer, const struct vk_cmd_blit_image_info *info)
{
	VkPipelineStageFlags src_stage_mask = 0;
	src_stage_mask |= info->src.src_stage_mask;
	src_stage_mask |= info->dst.src_stage_mask;

	VkImageMemoryBarrier barriers[2] = {
	    {
	        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	        .srcAccessMask = info->src.src_access_mask,
	        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
	        .oldLayout = info->src.old_layout,
	        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	        .image = info->src.fm_image.image,
	        .subresourceRange =
	            {
	                .aspectMask = info->src.fm_image.aspect_mask,
	                .baseMipLevel = 0,
	                .levelCount = 1,
	                .baseArrayLayer = info->src.fm_image.base_array_layer,
	                .layerCount = 1,
	            },
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	        .srcAccessMask = info->dst.src_access_mask,
	        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
	        .oldLayout = info->dst.old_layout,
	        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	        .image = info->dst.fm_image.image,
	        .subresourceRange =
	            {
	                .aspectMask = info->dst.fm_image.aspect_mask,
	                .baseMipLevel = 0,
	                .levelCount = 1,
	                .baseArrayLayer = info->dst.fm_image.base_array_layer,
	                .layerCount = 1,
	            },
	    },
	};

	vk->vkCmdPipelineBarrier(           //
	    cmd_buffer,                     // commandBuffer
	    src_stage_mask,                 // srcStageMask
	    VK_PIPELINE_STAGE_TRANSFER_BIT, // dstStageMask
	    0,                              // dependencyFlags
	    0,                              // memoryBarrierCount
	    NULL,                           // pMemoryBarriers
	    0,                              // bufferMemoryBarrierCount
	    NULL,                           // pBufferMemoryBarriers
	    ARRAY_SIZE(barriers),           // imageMemoryBarrierCount
	    barriers);                      // pImageMemoryBarriers

	// Specify the source region to blit from.
	VkImageSubresourceLayers src_subresource = {
	    .aspectMask = info->src.fm_image.aspect_mask,
	    .mipLevel = 0,
	    .baseArrayLayer = info->src.fm_image.base_array_layer,
	    .layerCount = 1,
	};

	// Specify the destination region to blit to.
	VkImageSubresourceLayers dst_subresource = {
	    .aspectMask = info->dst.fm_image.aspect_mask,
	    .mipLevel = 0,
	    .baseArrayLayer = info->dst.fm_image.base_array_layer,
	    .layerCount = 1,
	};

	int src_w1 = info->src.rect.offset.w;
	int src_h1 = info->src.rect.offset.h;
	int src_w2 = src_w1 + info->src.rect.extent.w;
	int src_h2 = src_h1 + info->src.rect.extent.h;

	int dst_w1 = info->dst.rect.offset.w;
	int dst_h1 = info->dst.rect.offset.h;
	int dst_w2 = dst_w1 + info->dst.rect.extent.w;
	int dst_h2 = dst_h1 + info->dst.rect.extent.h;

	VkImageBlit blit_region = {
	    .srcSubresource = src_subresource,
	    .srcOffsets[0] = {src_w1, src_h1, 0},
	    .srcOffsets[1] = {src_w2, src_h2, 1},
	    .dstSubresource = dst_subresource,
	    .dstOffsets[0] = {dst_w1, dst_h1, 0},
	    .dstOffsets[1] = {dst_w2, dst_h2, 1},
	};

	vk->vkCmdBlitImage(                       //
	    cmd_buffer,                           //
	    info->src.fm_image.image,             //
	    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, //
	    info->dst.fm_image.image,             //
	    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, //
	    1,                                    //
	    &blit_region,                         //
	    VK_FILTER_LINEAR);                    //
}

void
vk_cmd_blit_images_side_by_side_locked(struct vk_bundle *vk,
                                       VkCommandBuffer cmd_buffer,
                                       const struct vk_cmd_blit_images_side_by_side_info *info)
{
	VkPipelineStageFlags src_stage_mask = 0;
	src_stage_mask |= info->src[0].src_stage_mask;
	src_stage_mask |= info->src[1].src_stage_mask;
	src_stage_mask |= info->dst.src_stage_mask;

	VkImageMemoryBarrier barriers[3] = {
	    {
	        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	        .srcAccessMask = info->src[0].src_access_mask,
	        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
	        .oldLayout = info->src[0].old_layout,
	        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	        .image = info->src[0].fm_image.image,
	        .subresourceRange =
	            {
	                .aspectMask = info->src[0].fm_image.aspect_mask,
	                .baseMipLevel = 0,
	                .levelCount = 1,
	                .baseArrayLayer = info->src[0].fm_image.base_array_layer,
	                .layerCount = 1,
	            },
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	        .srcAccessMask = info->src[1].src_access_mask,
	        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
	        .oldLayout = info->src[1].old_layout,
	        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	        .image = info->src[1].fm_image.image,
	        .subresourceRange =
	            {
	                .aspectMask = info->src[1].fm_image.aspect_mask,
	                .baseMipLevel = 0,
	                .levelCount = 1,
	                .baseArrayLayer = info->src[1].fm_image.base_array_layer,
	                .layerCount = 1,
	            },
	    },
	    {
	        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	        .srcAccessMask = info->dst.src_access_mask,
	        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
	        .oldLayout = info->dst.old_layout,
	        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	        .image = info->dst.fm_image.image,
	        .subresourceRange =
	            {
	                .aspectMask = info->dst.fm_image.aspect_mask,
	                .baseMipLevel = 0,
	                .levelCount = 1,
	                .baseArrayLayer = info->dst.fm_image.base_array_layer,
	                .layerCount = 1,
	            },
	    },
	};

	vk->vkCmdPipelineBarrier(           //
	    cmd_buffer,                     // commandBuffer
	    src_stage_mask,                 // srcStageMask
	    VK_PIPELINE_STAGE_TRANSFER_BIT, // dstStageMask
	    0,                              // dependencyFlags
	    0,                              // memoryBarrierCount
	    NULL,                           // pMemoryBarriers
	    0,                              // bufferMemoryBarrierCount
	    NULL,                           // pBufferMemoryBarriers
	    ARRAY_SIZE(barriers),           // imageMemoryBarrierCount
	    barriers);                      // pImageMemoryBarriers

	// Specify the destination region to blit to.
	VkImageSubresourceLayers dst_subresource = {
	    .aspectMask = info->dst.fm_image.aspect_mask,
	    .mipLevel = 0,
	    .baseArrayLayer = info->dst.fm_image.base_array_layer,
	    .layerCount = 1,
	};

	int dst_y0 = 0;
	int dst_y1 = info->dst.size.h;
	int dst0_x0 = 0;
	int dst0_x1 = info->dst.size.w / 2;
	int dst1_x0 = info->dst.size.w / 2;
	int dst1_x1 = info->dst.size.w;

	VkOffset3D dst_offset[2][2] = {
	    {
	        {dst0_x0, dst_y0, 0},
	        {dst0_x1, dst_y1, 1},
	    },
	    {
	        {dst1_x0, dst_y0, 0},
	        {dst1_x1, dst_y1, 1},
	    },
	};

	for (int i = 0; i < 2; i++) {

		// Specify the source region to blit from.
		VkImageSubresourceLayers src_subresource = {
		    .aspectMask = info->src[i].fm_image.aspect_mask,
		    .mipLevel = 0,
		    .baseArrayLayer = info->src[i].fm_image.base_array_layer,
		    .layerCount = 1,
		};

		int w1 = info->src[i].rect.offset.w;
		int h1 = info->src[i].rect.offset.h;
		int w2 = w1 + info->src[i].rect.extent.w;
		int h2 = h1 + info->src[i].rect.extent.h;

		VkImageBlit blit_region = {
		    .srcSubresource = src_subresource,
		    .srcOffsets[0] = {w1, h1, 0},
		    .srcOffsets[1] = {w2, h2, 1},
		    .dstSubresource = dst_subresource,
		    .dstOffsets[0] = dst_offset[i][0],
		    .dstOffsets[1] = dst_offset[i][1],
		};

		vk->vkCmdBlitImage(                       //
		    cmd_buffer,                           //
		    info->src[i].fm_image.image,          //
		    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, //
		    info->dst.fm_image.image,             //
		    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, //
		    1,                                    //
		    &blit_region,                         //
		    VK_FILTER_LINEAR);                    //
	}
}
