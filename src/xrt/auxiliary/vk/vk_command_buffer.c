// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan command buffer helpers.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup aux_vk
 */

#include "vk/vk_helpers.h"


VkResult
vk_create_command_buffer(struct vk_bundle *vk, VkCommandPool pool, VkCommandBuffer *out_command_buffer)
{
	VkResult ret;

	VkCommandBufferAllocateInfo cmd_buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	    .commandPool = pool,
	    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	    .commandBufferCount = 1,
	};

	VkCommandBuffer cmd = VK_NULL_HANDLE;

	ret = vk->vkAllocateCommandBuffers( //
	    vk->device,                     // device
	    &cmd_buffer_info,               // pAllocateInfo
	    &cmd);                          // pCommandBuffers

	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateFramebuffer failed: %s", vk_result_string(ret));
		return ret;
	}

	*out_command_buffer = cmd;

	return VK_SUCCESS;
}

void
vk_destroy_command_buffer(struct vk_bundle *vk, VkCommandPool pool, VkCommandBuffer command_buffer)
{
	vk->vkFreeCommandBuffers( //
	    vk->device,           // device
	    pool,                 // commandPool
	    1,                    // commandBufferCount
	    &command_buffer);     // pCommandBuffers
}

VkResult
vk_begin_command_buffer(struct vk_bundle *vk, VkCommandBuffer command_buffer)
{
	VkResult ret;

	VkCommandBufferBeginInfo command_buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	ret = vk->vkBeginCommandBuffer( //
	    command_buffer,             // commandBuffer
	    &command_buffer_info);      // pBeginInfo
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkBeginCommandBuffer failed: %s", vk_result_string(ret));
		return ret;
	}

	return VK_SUCCESS;
}

VkResult
vk_end_command_buffer(struct vk_bundle *vk, VkCommandBuffer command_buffer)
{
	VkResult ret;

	// End the command buffer.
	ret = vk->vkEndCommandBuffer(command_buffer);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkEndCommandBuffer failed: %s", vk_result_string(ret));
		return ret;
	}

	return VK_SUCCESS;
}
