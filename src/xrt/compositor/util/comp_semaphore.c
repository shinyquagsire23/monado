// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Independent semaphore implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_util
 */

#include "util/comp_semaphore.h"


/*
 *
 * Member functions.
 *
 */

#ifdef VK_KHR_timeline_semaphore
static xrt_result_t
semaphore_wait(struct xrt_compositor_semaphore *xcsem, uint64_t value, uint64_t timeout_ns)
{
	struct comp_semaphore *csem = comp_semaphore(xcsem);
	struct vk_bundle *vk = csem->vk;
	VkResult ret;

	VkSemaphoreWaitInfo wait_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
	    .flags = 0,
	    .semaphoreCount = 1,
	    .pSemaphores = &csem->semaphore,
	    .pValues = &value,
	};

	ret = vk->vkWaitSemaphores( //
	    vk->device,             // device
	    &wait_info,             // pWaitInfo
	    timeout_ns);            // timeout
	if (ret == VK_TIMEOUT) {
		return XRT_TIMEOUT;
	}
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkWaitSemaphores: %s", vk_result_string(ret));
		return XRT_ERROR_VULKAN;
	}

	return XRT_SUCCESS;
}

static void
semaphore_destroy(struct xrt_compositor_semaphore *xcsem)
{
	struct comp_semaphore *csem = comp_semaphore(xcsem);
	struct vk_bundle *vk = csem->vk;

	vk->vkDestroySemaphore( //
	    vk->device,         // device
	    csem->semaphore,    // semaphore
	    NULL);              // pAllocator

	free(csem);
}
#endif


/*
 *
 * 'Exported' functions.
 *
 */

xrt_result_t
comp_semaphore_create(struct vk_bundle *vk,
                      xrt_graphics_sync_handle_t *out_handle,
                      struct xrt_compositor_semaphore **out_xcsem)
{
#ifdef VK_KHR_timeline_semaphore
	VkResult ret;

	if (!vk->features.timeline_semaphore) {
		return XRT_ERROR_VULKAN;
	}

	VkExternalSemaphoreHandleTypeFlags handle_type = 0;


#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	if (!vk->external.timeline_semaphore_opaque_fd) {
		VK_ERROR(vk, "External timeline semaphore opaque fd not supported!");
		return XRT_ERROR_VULKAN;
	} else {
		handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
	}
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	if (!vk->external.timeline_semaphore_win32_handle) {
		VK_ERROR(vk, "External timeline semaphore win32 handle not supported!");
		return XRT_ERROR_VULKAN;
	} else {
		handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
	}
#else
#error "Need to port semaphore creation code."
#endif


	VkExportSemaphoreCreateInfo export_info = {
	    .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
	    .handleTypes = handle_type,
	};

	VkSemaphoreTypeCreateInfo type_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
	    .pNext = &export_info,
	    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
	    .initialValue = 0,
	};

	VkSemaphoreCreateInfo semaphore_create_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	    .pNext = &type_info,
	    .flags = 0,
	};

	VkSemaphore semaphore = VK_NULL_HANDLE;
	ret = vk->vkCreateSemaphore( //
	    vk->device,              // dev
	    &semaphore_create_info,  // pCreateInfo
	    NULL,                    // pAllocator
	    &semaphore);             // pSemaphore
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateSemaphore: %s", vk_result_string(ret));
		return XRT_ERROR_VULKAN;
	}


#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	VkSemaphoreGetFdInfoKHR get_fd_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
	    .semaphore = semaphore,
	    .handleType = handle_type,
	};

	ret = vk->vkGetSemaphoreFdKHR(vk->device, &get_fd_info, out_handle);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkGetSemaphoreFdKHR: %s", vk_result_string(ret));
		vk->vkDestroySemaphore(vk->device, semaphore, NULL);
		return XRT_ERROR_VULKAN;
	}
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
#error "No windows port"
#else
#error "Need to port semaphore creation code."
#endif


	struct comp_semaphore *csem = U_TYPED_CALLOC(struct comp_semaphore);

	csem->base.reference.count = 1;
	csem->base.destroy = semaphore_destroy;
	csem->base.wait = semaphore_wait;
	csem->semaphore = semaphore;
	csem->vk = vk;

	*out_xcsem = &csem->base;

	return XRT_SUCCESS;
#else
	// How did you even get here?
	VK_ERROR(vk, "No compile time support for VK_KHR_timeline_semaphore!");
	return XRT_ERROR_VULKAN;
#endif
}
