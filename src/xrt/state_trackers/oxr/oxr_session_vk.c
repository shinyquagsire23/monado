// Copyright 2018-2020, Collabora, Ltd.
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

#include "xrt/xrt_instance.h"
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
	struct xrt_compositor_fd *xcfd = NULL;
	struct xrt_device *xdev = GET_XDEV_BY_ROLE(sess->sys, head);

	int ret = xrt_instance_create_fd_compositor(sys->inst->xinst, xdev,
	                                            false, &xcfd);
	if (ret < 0 || xcfd == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 "Failed to create an fd compositor '%i'", ret);
	}

	struct xrt_compositor_vk *xcvk = xrt_gfx_vk_provider_create(
	    xcfd, next->instance, vkGetInstanceProcAddr, next->physicalDevice,
	    next->device, next->queueFamilyIndex, next->queueIndex);

	if (xcvk == NULL) {
		xcfd->base.destroy(&xcfd->base);
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 "Failed to create an vk client compositor");
	}

	sess->compositor = &xcvk->base;
	sess->create_swapchain = oxr_swapchain_vk_create;

	return XR_SUCCESS;
}
