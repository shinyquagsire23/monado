// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds Vulkan swapchain related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#include <stdlib.h>

#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"


static XrResult
oxr_swapchain_vk_enumerate_images(struct oxr_logger *log,
                                  struct oxr_swapchain *sc,
                                  uint32_t count,
                                  XrSwapchainImageBaseHeader *images)
{
	struct xrt_swapchain_vk *xscvk = (struct xrt_swapchain_vk *)sc->swapchain;
	XrSwapchainImageVulkanKHR *vk_imgs = (XrSwapchainImageVulkanKHR *)images;

	for (uint32_t i = 0; i < count; i++) {
		vk_imgs[i].image = xscvk->images[i];
	}

	return oxr_session_success_result(sc->sess);
}

XrResult
oxr_swapchain_vk_create(struct oxr_logger *log,
                        struct oxr_session *sess,
                        const XrSwapchainCreateInfo *createInfo,
                        struct oxr_swapchain **out_swapchain)
{
	struct oxr_swapchain *sc;
	XrResult ret;

	ret = oxr_create_swapchain(log, sess, createInfo, &sc);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	// Set our API specific function(s).
	sc->enumerate_images = oxr_swapchain_vk_enumerate_images;

	*out_swapchain = sc;

	return XR_SUCCESS;
}
