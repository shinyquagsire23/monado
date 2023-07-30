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
#include "vk/vk_cmd_pool.h"


/*
 *
 * 'Exported' functions.
 *
 */

XRT_CHECK_RESULT VkResult
vk_cmd_pool_init(struct vk_bundle *vk, struct vk_cmd_pool *pool, VkCommandPoolCreateFlags flags)
{
	VkResult ret;

	XRT_MAYBE_UNUSED int iret = os_mutex_init(&pool->mutex);
	assert(iret == 0);

	VkCommandPoolCreateInfo cmd_pool_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
	    .flags = flags,
	    .queueFamilyIndex = vk->queue_family_index,
	};

	ret = vk->vkCreateCommandPool(vk->device, &cmd_pool_info, NULL, &pool->pool);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateCommandPool: %s", vk_result_string(ret));
		os_mutex_destroy(&pool->mutex);
	}

	return ret;
}

void
vk_cmd_pool_destroy(struct vk_bundle *vk, struct vk_cmd_pool *pool)
{
	// Early out if never created.
	if (pool->pool == VK_NULL_HANDLE) {
		return;
	}

	vk->vkDestroyCommandPool(vk->device, pool->pool, NULL);
	pool->pool = VK_NULL_HANDLE;

	os_mutex_destroy(&pool->mutex);
}

XRT_CHECK_RESULT VkResult
vk_cmd_pool_create_cmd_buffer_locked(struct vk_bundle *vk, struct vk_cmd_pool *pool, VkCommandBuffer *out_cmd_buffer)
{
	VkCommandBuffer cmd_buffer;
	VkResult ret;

	// Allocate the command buffer.
	VkCommandBufferAllocateInfo cmd_buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	    .commandPool = pool->pool,
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
vk_cmd_pool_create_and_begin_cmd_buffer_locked(struct vk_bundle *vk,
                                               struct vk_cmd_pool *pool,
                                               VkCommandBufferUsageFlags flags,
                                               VkCommandBuffer *out_cmd_buffer)
{
	VkCommandBuffer cmd_buffer;
	VkResult ret;

	ret = vk_cmd_pool_create_cmd_buffer_locked(vk, pool, &cmd_buffer);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vk_cmd_pool_create_cmd_buffer_locked: %s", vk_result_string(ret));
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
	vk->vkFreeCommandBuffers(vk->device, pool->pool, 1, &cmd_buffer);

	return ret;
}

XRT_CHECK_RESULT VkResult
vk_cmd_pool_submit_cmd_buffer_locked(struct vk_bundle *vk, struct vk_cmd_pool *pool, VkCommandBuffer cmd_buffer)
{
	VkResult ret;

	// Do the submit.
	VkSubmitInfo submitInfo = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &cmd_buffer,
	};

	ret = vk_cmd_submit_locked(vk, 1, &submitInfo, VK_NULL_HANDLE);

	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vk_cmd_submit_locked: %s", vk_result_string(ret));
	}

	return ret;
}
