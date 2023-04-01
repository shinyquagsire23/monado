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
