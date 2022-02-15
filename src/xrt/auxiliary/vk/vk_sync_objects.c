// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan sync primitives code.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_vk
 */

#include <xrt/xrt_handles.h>

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "vk/vk_helpers.h"


VkResult
vk_create_fence_sync_from_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t native, VkFence *out_fence)
{
	VkFence fence = VK_NULL_HANDLE;
	VkResult ret;

	VkFenceCreateInfo create_info = {
	    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};

	ret = vk->vkCreateFence(vk->device, &create_info, NULL, &fence);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateFence: %s", vk_result_string(ret));
		return ret;
	}

#ifdef XRT_GRAPHICS_SYNC_HANDLE_IS_FD
	// This is what is used on Linux Mesa when importing fences from OpenGL.
	VkExternalFenceHandleTypeFlagBits handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;

	VkImportFenceFdInfoKHR import_info = {
	    .sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR,
	    .fence = fence,
	    .handleType = handleType,
	    .fd = native,
	};

	ret = vk->vkImportFenceFdKHR(vk->device, &import_info);
	if (ret != VK_SUCCESS) {
		vk->vkDestroyFence(vk->device, fence, NULL);
		VK_ERROR(vk, "vkImportFenceFdKHR: %s", vk_result_string(ret));
		return ret;
	}
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	//! @todo make sure this is the right one
	VkExternalFenceHandleTypeFlagBits handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
	VkImportFenceWin32HandleInfoKHR import_info = {
	    .sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_WIN32_HANDLE_INFO_KHR,
	    .pNext = NULL,
	    .fence = fence,
	    .flags = 0, /** @todo do we want the temporary flag? */
	    .handleType = handleType,
	    .handle = native,
	    .name = NULL, /* not importing by name */
	};

	ret = vk->vkImportFenceWin32HandleKHR(vk->device, &import_info);
	if (ret != VK_SUCCESS) {
		vk->vkDestroyFence(vk->device, fence, NULL);
		VK_ERROR(vk, "vkImportFenceFdKHR: %s", vk_result_string(ret));
		return ret;
	}
#else
#error "Need port to import fence sync handles"
#endif

	*out_fence = fence;

	return VK_SUCCESS;
}

VkResult
vk_create_semaphore_from_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t native, VkSemaphore *out_sem)
{
	VkResult ret;

	VkSemaphoreCreateInfo semaphore_create_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	ret = vk->vkCreateSemaphore(vk->device, &semaphore_create_info, NULL, out_sem);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateSemaphore: %s", vk_result_string(ret));
		// Nothing to cleanup
		return ret;
	}
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	VkImportSemaphoreFdInfoKHR import_semaphore_fd_info = {
	    .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
	    .semaphore = *out_sem,
	    .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
	    .fd = native,
	};
	ret = vk->vkImportSemaphoreFdKHR(vk->device, &import_semaphore_fd_info);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkImportSemaphoreFdKHR: %s", vk_result_string(ret));
		vk->vkDestroySemaphore(vk->device, *out_sem, NULL);
		return ret;
	}
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	VkImportSemaphoreWin32HandleInfoKHR import_semaphore_handle_info = {
	    .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR,
	    .semaphore = *out_sem,
	    .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT,
	    .handle = native,
	};
	ret = vk->vkImportSemaphoreWin32HandleKHR(vk->device, &import_semaphore_handle_info);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkImportSemaphoreWin32HandleKHR: %s", vk_result_string(ret));
		vk->vkDestroySemaphore(vk->device, *out_sem, NULL);
		return ret;
	}
#else
#error "Not implemented for this underlying handle type!"
#endif
	return ret;
}
