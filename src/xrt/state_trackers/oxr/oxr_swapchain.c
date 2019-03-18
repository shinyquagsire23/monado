// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds swapchain related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include <stdlib.h>

#include "xrt/xrt_gfx_xlib.h"
#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"


static XrResult
oxr_swapchain_acquire_image(struct oxr_logger *log,
                            struct oxr_swapchain *sc,
                            const XrSwapchainImageAcquireInfo *acquireInfo,
                            uint32_t *out_index)
{
	uint32_t index;
	if (sc->acquired_index >= 0) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID,
		                 " image already acquired");
	}

	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;
	if (!xsc->acquire_image(xsc, &index)) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 " call to xsc->acquire_image failed");
	}

	sc->acquired_index = (int)index;
	*out_index = index;

	return XR_SUCCESS;
}

static XrResult
oxr_swapchain_wait_image(struct oxr_logger *log,
                         struct oxr_swapchain *sc,
                         const XrSwapchainImageWaitInfo *waitInfo)
{
	if (sc->acquired_index < 0) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID,
		                 " no image acquired");
	}

	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;
	if (!xsc->wait_image(xsc, waitInfo->timeout,
	                     (uint32_t)sc->acquired_index)) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 " call to xsc->wait_image failed");
	}

	return XR_SUCCESS;
}

static XrResult
oxr_swapchain_release_image(struct oxr_logger *log,
                            struct oxr_swapchain *sc,
                            const XrSwapchainImageReleaseInfo *releaseInfo)
{
	if (sc->acquired_index < 0) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID,
		                 " no image acquired");
	}

	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;
	if (!xsc->release_image(xsc, (uint32_t)sc->acquired_index)) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 " call to xsc->release_image failed");
	}
	sc->acquired_index = -1;

	return XR_SUCCESS;
}

XrResult
oxr_create_swapchain(struct oxr_logger *log,
                     struct oxr_session *sess,
                     const XrSwapchainCreateInfo *createInfo,
                     struct oxr_swapchain **out_swapchain)
{
	struct xrt_swapchain *xsc = sess->compositor->create_swapchain(
	    sess->compositor,
	    (enum xrt_swapchain_create_flags)createInfo->createFlags,
	    (enum xrt_swapchain_usage_bits)createInfo->usageFlags,
	    createInfo->format, createInfo->sampleCount, createInfo->width,
	    createInfo->height, createInfo->faceCount, createInfo->arraySize,
	    createInfo->mipCount);

	if (xsc == NULL) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 " failed to create swapchain");
	}

	struct oxr_swapchain *sc =
	    (struct oxr_swapchain *)calloc(1, sizeof(struct oxr_swapchain));
	sc->debug = OXR_XR_DEBUG_SWAPCHAIN;
	sc->sess = sess;
	sc->swapchain = xsc;
	sc->acquire_image = oxr_swapchain_acquire_image;
	sc->wait_image = oxr_swapchain_wait_image;
	sc->release_image = oxr_swapchain_release_image;
	sc->acquired_index = -1;

	*out_swapchain = sc;

	return XR_SUCCESS;
}
