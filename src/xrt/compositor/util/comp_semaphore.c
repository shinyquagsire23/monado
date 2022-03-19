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

	VkSemaphore semaphore;
	ret = vk_create_timeline_semaphore_and_native(vk, &semaphore, out_handle);
	if (ret != VK_SUCCESS) {
		return XRT_ERROR_VULKAN;
	}


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
