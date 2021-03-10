// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Sync code for the main compositor.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include "util/u_misc.h"
#include "util/u_handles.h"

#include "main/comp_compositor.h"

#include <xrt/xrt_handles.h>
#include <xrt/xrt_config_os.h>

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>



struct fence
{
	struct xrt_compositor_fence base;
	struct comp_compositor *c;

	VkFence fence;
};


/*
 *
 * Helper functions.
 *
 */



/*
 *
 * Fence member functions.
 *
 */

static xrt_result_t
fence_wait(struct xrt_compositor_fence *xcf, uint64_t timeout)
{
	COMP_TRACE_MARKER();

	struct fence *f = (struct fence *)xcf;
	struct vk_bundle *vk = &f->c->vk;

	// Count no handle as signled fence.
	if (f->fence == VK_NULL_HANDLE) {
		return XRT_SUCCESS;
	}

	VkResult ret = vk->vkWaitForFences(vk->device, 1, &f->fence, VK_TRUE, timeout);
	if (ret == VK_TIMEOUT) {
		return XRT_SUCCESS;
	}
	if (ret != VK_SUCCESS) {
		COMP_ERROR(f->c, "vkWaitForFences: %s", vk_result_string(ret));
		return XRT_ERROR_VULKAN;
	}

	return XRT_SUCCESS;
}

static void
fence_destroy(struct xrt_compositor_fence *xcf)
{
	COMP_TRACE_MARKER();

	struct fence *f = (struct fence *)xcf;
	struct vk_bundle *vk = &f->c->vk;

	if (f->fence != VK_NULL_HANDLE) {
		vk->vkDestroyFence(vk->device, f->fence, NULL);
		f->fence = VK_NULL_HANDLE;
	}

	free(f);
}


/*
 *
 * Compositor function.
 *
 */

xrt_result_t
comp_compositor_import_fence(struct xrt_compositor *xc,
                             xrt_graphics_sync_handle_t handle,
                             struct xrt_compositor_fence **out_xcf)
{
	COMP_TRACE_MARKER();

	struct comp_compositor *c = comp_compositor(xc);
	struct vk_bundle *vk = &c->vk;

	VkFence fence = VK_NULL_HANDLE;

	VkResult ret = vk_create_fence_sync_from_native(vk, handle, &fence);
	if (ret != VK_SUCCESS) {
		return XRT_ERROR_VULKAN;
	}

	struct fence *f = U_TYPED_CALLOC(struct fence);
	f->base.wait = fence_wait;
	f->base.destroy = fence_destroy;
	f->fence = fence;
	f->c = c;

	*out_xcf = &f->base;

	return XRT_SUCCESS;
}
