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


/*
 *
 * Helper functions.
 *
 */

static VkExternalSemaphoreHandleTypeFlagBits
vk_get_semaphore_handle_type(struct vk_bundle *vk)
{
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	if (vk->external.binary_semaphore_opaque_fd) {
		return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
	}

#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	if (vk->external.binary_semaphore_d3d12_fence) {
		return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
	}
	if (vk->external.binary_semaphore_win32_handle) {
		return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
	}
#else
#error "Need to port semaphore type code."
#endif
	return 0;
}

#ifdef VK_KHR_timeline_semaphore
static VkExternalSemaphoreHandleTypeFlagBits
vk_get_timeline_semaphore_handle_type(struct vk_bundle *vk)
{
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	if (vk->external.timeline_semaphore_opaque_fd) {
		return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
	}
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	if (vk->external.timeline_semaphore_d3d12_fence) {
		return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
	}
	if (vk->external.timeline_semaphore_win32_handle) {
		return VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
	}
#else
#error "Need to port semaphore type code."
#endif
	return 0;
}
#endif


/*
 *
 * Check functions.
 *
 */

XRT_CHECK_RESULT bool
vk_can_import_and_export_timeline_semaphore(struct vk_bundle *vk)
{
#ifdef VK_KHR_timeline_semaphore
	// Timeline semaphore extension and feature been enabled?
	if (!vk->features.timeline_semaphore) {
		return false;
	}

	// Supported handle type for import/export?
	if (vk_get_timeline_semaphore_handle_type(vk) == 0) {
		return false;
	}

	// Everything points to yes!
	return true;
#else
	return false;
#endif
}


/*
 *
 * Export.
 *
 */

XRT_CHECK_RESULT VkResult
vk_create_and_submit_fence_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t *out_native)
{
	xrt_graphics_sync_handle_t native = XRT_GRAPHICS_SYNC_HANDLE_INVALID;
	VkFence fence = VK_NULL_HANDLE;
	VkResult ret;

#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	const VkExternalFenceHandleTypeFlags handle_type = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	const VkExternalFenceHandleTypeFlags handle_type = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
#error "Need port to export fence sync handles"
#endif

	VkExportFenceCreateInfo export_create_info = {
	    .sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,
	    .pNext = NULL,
	    .handleTypes = handle_type,
	};

	VkFenceCreateInfo create_info = {
	    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	    .flags = 0, // Not signalled.
	    .pNext = (const void *)&export_create_info,
	};

	ret = vk->vkCreateFence(vk->device, &create_info, NULL, &fence);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateFence: %s", vk_result_string(ret));
		return ret;
	}


	/*
	 * Submit fence.
	 */

	os_mutex_lock(&vk->queue_mutex);

	ret = vk->vkQueueSubmit( //
	    vk->queue,           // queue
	    0,                   // submitCount
	    NULL,                // pSubmits
	    fence);              // fence

	os_mutex_unlock(&vk->queue_mutex);

	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkQueueSubmit: %s", vk_result_string(ret));
		vk->vkDestroyFence(vk->device, fence, NULL);
		return ret;
	}


	/*
	 * Get native handle.
	 */

#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	VkFenceGetFdInfoKHR get_fd_info = {
	    .sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR,
	    .fence = fence,
	    .handleType = handle_type,
	};

	ret = vk->vkGetFenceFdKHR(vk->device, &get_fd_info, &native);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkGetFenceFdKHR: %s", vk_result_string(ret));
		vk->vkDestroyFence(vk->device, fence, NULL);
		return ret;
	}
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	//! @todo Not tested.
	VkFenceGetWin32HandleInfoKHR get_handle_info = {
	    .sType = VK_STRUCTURE_TYPE_FENCE_GET_WIN32_HANDLE_INFO_KHR,
	    .fence = fence,
	    .handleType = handle_type,
	};

	ret = vk->vkGetFenceWin32HandleKHR(vk->device, &get_handle_info, &native);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkGetFenceWin32HandleKHR: %s", vk_result_string(ret));
		vk->vkDestroyFence(vk->device, fence, NULL);
		return ret;
	}
#else
#error "Need port to import fence sync handles"
#endif


	/*
	 * Clean up
	 */

	vk->vkDestroyFence(vk->device, fence, NULL);
	fence = VK_NULL_HANDLE;

	//*out_fence = fence;
	*out_native = native;

	return VK_SUCCESS;
}

XRT_CHECK_RESULT static VkResult
create_semaphore_and_native(struct vk_bundle *vk,
                            VkExternalSemaphoreHandleTypeFlagBits handle_type,
                            const void *next,
                            VkSemaphore *out_sem,
                            xrt_graphics_sync_handle_t *out_native)
{
	xrt_graphics_sync_handle_t native = XRT_GRAPHICS_SYNC_HANDLE_INVALID;
	VkSemaphore semaphore = VK_NULL_HANDLE;
	VkResult ret;

	VkExportSemaphoreCreateInfo export_info = {
	    .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
	    .pNext = next,
	    .handleTypes = handle_type,
	};

	VkSemaphoreCreateInfo semaphore_create_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	    .pNext = &export_info,
	    .flags = 0,
	};

	ret = vk->vkCreateSemaphore( //
	    vk->device,              // dev
	    &semaphore_create_info,  // pCreateInfo
	    NULL,                    // pAllocator
	    &semaphore);             // pSemaphore
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateSemaphore: %s", vk_result_string(ret));
		return ret;
	}


#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	VkSemaphoreGetFdInfoKHR get_fd_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
	    .semaphore = semaphore,
	    .handleType = handle_type,
	};

	ret = vk->vkGetSemaphoreFdKHR(vk->device, &get_fd_info, &native);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkGetSemaphoreFdKHR: %s", vk_result_string(ret));
		vk->vkDestroySemaphore(vk->device, semaphore, NULL);
		return ret;
	}
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	VkSemaphoreGetWin32HandleInfoKHR get_handle_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR,
	    .semaphore = semaphore,
	    .handleType = handle_type,
	};

	ret = vk->vkGetSemaphoreWin32HandleKHR(vk->device, &get_handle_info, &native);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkGetSemaphoreWin32HandleKHR: %s", vk_result_string(ret));
		vk->vkDestroySemaphore(vk->device, semaphore, NULL);
		return ret;
	}
#else
#error "Need to port semaphore creation code."
#endif

	// All done, pass ownership.
	*out_sem = semaphore;
	*out_native = native;

	return VK_SUCCESS;
}

XRT_CHECK_RESULT VkResult
vk_create_semaphore_and_native(struct vk_bundle *vk, VkSemaphore *out_sem, xrt_graphics_sync_handle_t *out_native)
{
	VkExternalSemaphoreHandleTypeFlagBits handle_type = 0;

	handle_type = vk_get_semaphore_handle_type(vk);
	if (handle_type == 0) {
		VK_ERROR(vk, "No semaphore type supported for export/import.");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	return create_semaphore_and_native( //
	    vk,                             // vk_bundle
	    handle_type,                    // handle_type
	    NULL,                           // next
	    out_sem,                        // out_sem
	    out_native);                    // out_native
}

#ifdef VK_KHR_timeline_semaphore
XRT_CHECK_RESULT VkResult
vk_create_timeline_semaphore_and_native(struct vk_bundle *vk,
                                        VkSemaphore *out_sem,
                                        xrt_graphics_sync_handle_t *out_native)
{
	VkExternalSemaphoreHandleTypeFlagBits handle_type = 0;

	handle_type = vk_get_timeline_semaphore_handle_type(vk);
	if (handle_type == 0) {
		VK_ERROR(vk, "No timeline semaphore type supported for export/import.");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkSemaphoreTypeCreateInfo type_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
	    .pNext = NULL,
	    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
	    .initialValue = 0,
	};

	return create_semaphore_and_native( //
	    vk,                             // vk_bundle
	    handle_type,                    // handle_type
	    &type_info,                     // next
	    out_sem,                        // out_sem
	    out_native);                    // out_native
}
#endif


/*
 *
 * Import.
 *
 */

XRT_CHECK_RESULT VkResult
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

XRT_CHECK_RESULT static VkResult
create_semaphore_from_native(struct vk_bundle *vk,
                             VkExternalSemaphoreHandleTypeFlagBits handle_type,
                             const void *next,
                             xrt_graphics_sync_handle_t native,
                             VkSemaphore *out_sem)
{
	VkResult ret;

	VkSemaphoreCreateInfo semaphore_create_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	    .pNext = next,
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
	    .handleType = handle_type,
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
	    .handleType = handle_type,
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

	return VK_SUCCESS;
}

XRT_CHECK_RESULT VkResult
vk_create_semaphore_from_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t native, VkSemaphore *out_sem)
{
	VkExternalSemaphoreHandleTypeFlagBits handle_type = 0;

#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
#error "Not implemented for this underlying handle type!"
#endif

	return create_semaphore_from_native( //
	    vk,                              // vk_bundle
	    handle_type,                     // handle_type
	    NULL,                            // next
	    native,                          // native
	    out_sem);                        // out_sem
}

#ifdef VK_KHR_timeline_semaphore
XRT_CHECK_RESULT VkResult
vk_create_timeline_semaphore_from_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t native, VkSemaphore *out_sem)
{
	VkExternalSemaphoreHandleTypeFlagBits handle_type = 0;

	handle_type = vk_get_timeline_semaphore_handle_type(vk);
	if (handle_type == 0) {
		VK_ERROR(vk, "No timeline semaphore type supported for export/import.");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkSemaphoreTypeCreateInfo type_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
	    .pNext = NULL,
	    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
	    .initialValue = 0,
	};

	return create_semaphore_from_native( //
	    vk,                              // vk_bundle
	    handle_type,                     // handle_type
	    &type_info,                      // next
	    native,                          // native
	    out_sem);                        // out_sem
}
#endif
