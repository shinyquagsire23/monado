// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds Vulkan specific session functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#include <stdlib.h>

#include "util/u_misc.h"

#include "xrt/xrt_gfx_fd.h"
#include "xrt/xrt_gfx_vk.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_handle.h"

XrResult
oxr_session_populate_vk(struct oxr_logger *log,
                        struct oxr_system *sys,
                        XrGraphicsBindingVulkanKHR const *next,
                        struct oxr_session *sess)
{
	struct xrt_compositor_fd *xcfd =
	    xrt_gfx_provider_create_fd(sys->head, false);
	if (xcfd == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 " failed create a fd compositor");
	}

	struct xrt_compositor_vk *xcvk = xrt_gfx_vk_provider_create(
	    xcfd, next->instance, vkGetInstanceProcAddr, next->physicalDevice,
	    next->device, next->queueFamilyIndex, next->queueIndex);

	if (xcvk == NULL) {
		xcfd->base.destroy(&xcfd->base);
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 " failed create a vk client compositor");
	}

	sess->compositor = &xcvk->base;
	sess->create_swapchain = oxr_swapchain_vk_create;

	return XR_SUCCESS;
}
