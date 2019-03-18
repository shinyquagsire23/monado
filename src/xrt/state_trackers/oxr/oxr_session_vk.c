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

#include "xrt/xrt_gfx_vk.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"


VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName);

XrResult
oxr_session_create_vk(struct oxr_logger *log,
                      struct oxr_system *sys,
                      XrGraphicsBindingVulkanKHR *next,
                      struct oxr_session **out_session)
{
	struct xrt_compositor_vk *xcvk = xrt_gfx_vk_provider_create(
	    sys->device, next->instance, vkGetInstanceProcAddr,
	    next->physicalDevice, next->device, next->queueFamilyIndex,
	    next->queueIndex);

	if (xcvk == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 " failed create a compositor");
	}

	struct oxr_session *sess =
	    (struct oxr_session *)calloc(1, sizeof(struct oxr_session));
	sess->debug = OXR_XR_DEBUG_SESSION;
	sess->sys = sys;
	sess->compositor = &xcvk->base;
	sess->create_swapchain = oxr_swapchain_vk_create;

	*out_session = sess;

	return XR_SUCCESS;
}
