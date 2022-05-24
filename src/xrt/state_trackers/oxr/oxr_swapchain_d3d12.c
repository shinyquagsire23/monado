// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds D3D12 swapchain related functions.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#include <stdlib.h>

#include "xrt/xrt_gfx_d3d12.h"
#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_api_verify.h"


static XrResult
oxr_swapchain_d3d12_destroy(struct oxr_logger *log, struct oxr_swapchain *sc)
{
	// Release any waited image.
	if (sc->waited.yes) {
		sc->release_image(log, sc, NULL);
	}

	// Release any acquired images.
	XrSwapchainImageWaitInfo waitInfo = {0};
	while (!u_index_fifo_is_empty(&sc->acquired.fifo)) {
		sc->wait_image(log, sc, &waitInfo);
		sc->release_image(log, sc, NULL);
	}

	// Drop our reference, does NULL checking.
	xrt_swapchain_reference(&sc->swapchain, NULL);

	return XR_SUCCESS;
}

static XrResult
oxr_swapchain_d3d12_enumerate_images(struct oxr_logger *log,
                                     struct oxr_swapchain *sc,
                                     uint32_t count,
                                     XrSwapchainImageBaseHeader *images)
{
	struct xrt_swapchain_d3d12 *xscd3d = (struct xrt_swapchain_d3d12 *)sc->swapchain;
	XrSwapchainImageD3D12KHR *d3d_imgs = (XrSwapchainImageD3D12KHR *)images;

	for (uint32_t i = 0; i < count; i++) {
		d3d_imgs[i].texture = xscd3d->images[i];
	}

	return oxr_session_success_result(sc->sess);
}

XrResult
oxr_swapchain_d3d12_create(struct oxr_logger *log,
                           struct oxr_session *sess,
                           const XrSwapchainCreateInfo *createInfo,
                           struct oxr_swapchain **out_swapchain)
{
	struct oxr_swapchain *sc;
	XrResult ret;

	OXR_VERIFY_SWAPCHAIN_USAGE_FLAGS_NOT_MUTUALLY_EXCLUSIVE(log, createInfo->usageFlags,
	                                                        XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
	                                                        XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	OXR_VERIFY_SWAPCHAIN_USAGE_FLAGS_NOT_MUTUALLY_EXCLUSIVE(log, createInfo->usageFlags,
	                                                        XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT,
	                                                        XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

	ret = oxr_create_swapchain(log, sess, createInfo, &sc);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	sc->destroy = oxr_swapchain_d3d12_destroy;
	sc->enumerate_images = oxr_swapchain_d3d12_enumerate_images;

	*out_swapchain = sc;

	return XR_SUCCESS;
}
