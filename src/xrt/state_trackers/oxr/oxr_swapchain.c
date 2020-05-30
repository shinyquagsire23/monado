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
#include "util/u_misc.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_handle.h"


static XrResult
oxr_swapchain_acquire_image(struct oxr_logger *log,
                            struct oxr_swapchain *sc,
                            const XrSwapchainImageAcquireInfo *acquireInfo,
                            uint32_t *out_index)
{
	uint32_t index;
	if (sc->acquired.num >= sc->swapchain->num_images) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID,
		                 " all images have been acquired");
	}

	if (sc->is_static && (sc->released.yes || sc->waited.yes)) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID,
		                 "Can only acquire once on a static swapchain");
	}

	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;
	if (!xsc->acquire_image(xsc, &index)) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 " call to xsc->acquire_image failed");
	}

	if (sc->images[index].state != OXR_IMAGE_STATE_READY) {
		return oxr_error(
		    log, XR_ERROR_RUNTIME_FAILURE,
		    "Internal acquire call returned non-ready image.");
	}

	sc->acquired.num++;
	u_index_fifo_push(&sc->acquired.fifo, index);
	sc->images[index].state = OXR_IMAGE_STATE_ACQUIRED;

	// If the compositor is resuing the image,
	// mark it as invalid to use in xrEndFrame.
	if (sc->released.index == (int)index) {
		sc->released.yes = false;
		sc->released.index = -1;
	}

	*out_index = index;

	return oxr_session_success_result(sc->sess);
}

static XrResult
oxr_swapchain_wait_image(struct oxr_logger *log,
                         struct oxr_swapchain *sc,
                         const XrSwapchainImageWaitInfo *waitInfo)
{
	if (sc->waited.yes) {
		return oxr_error(
		    log, XR_ERROR_CALL_ORDER_INVALID,
		    " swapchain has already been waited, call release");
	}

	if (u_index_fifo_is_empty(&sc->acquired.fifo)) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID,
		                 " no image acquired");
	}

	uint32_t index;
	u_index_fifo_pop(&sc->acquired.fifo, &index);

	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;
	if (!xsc->wait_image(xsc, waitInfo->timeout, index)) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 " call to xsc->wait_image failed");
	}

	// The app can only wait on one image.
	sc->waited.yes = true;
	sc->waited.index = index;
	sc->images[index].state = OXR_IMAGE_STATE_WAITED;

	return oxr_session_success_result(sc->sess);
}

static XrResult
oxr_swapchain_release_image(struct oxr_logger *log,
                            struct oxr_swapchain *sc,
                            const XrSwapchainImageReleaseInfo *releaseInfo)
{
	if (!sc->waited.yes) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID,
		                 " no swapchain images waited on");
	}

	sc->waited.yes = false;
	uint32_t index = sc->waited.index;

	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;
	if (!xsc->release_image(xsc, index)) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 " call to xsc->release_image failed");
	}

	// Only decerement here.
	sc->acquired.num--;

	// Overwrite the old released image, with new.
	sc->released.yes = true;
	sc->released.index = index;
	sc->images[index].state = OXR_IMAGE_STATE_READY;

	return oxr_session_success_result(sc->sess);
}

static XrResult
oxr_swapchain_destroy(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	struct oxr_swapchain *sc = (struct oxr_swapchain *)hb;

	XrResult ret = sc->destroy(log, sc);
	free(sc);
	return ret;
}

static enum xrt_swapchain_create_flags
convert_create_flags(XrSwapchainCreateFlags xr_flags)
{
	enum xrt_swapchain_create_flags flags = 0;
	if ((xr_flags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) != 0) {
		flags |= XRT_SWAPCHAIN_CREATE_STATIC_IMAGE;
	}

	return flags;
}

static enum xrt_swapchain_usage_bits
convert_usage_bits(XrSwapchainUsageFlags xr_usage)
{
	enum xrt_swapchain_usage_bits usage = 0;

	if ((xr_usage & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) != 0) {
		usage |= XRT_SWAPCHAIN_USAGE_COLOR;
	}
	if ((xr_usage & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
		usage |= XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL;
	}
	if ((xr_usage & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT) != 0) {
		usage |= XRT_SWAPCHAIN_USAGE_UNORDERED_ACCESS;
	}
	if ((xr_usage & XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT) != 0) {
		usage |= XRT_SWAPCHAIN_USAGE_TRANSFER_SRC;
	}
	if ((xr_usage & XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT) != 0) {
		usage |= XRT_SWAPCHAIN_USAGE_TRANSFER_DST;
	}
	if ((xr_usage & XR_SWAPCHAIN_USAGE_SAMPLED_BIT) != 0) {
		usage |= XRT_SWAPCHAIN_USAGE_SAMPLED;
	}
	if ((xr_usage & XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT) != 0) {
		usage |= XRT_SWAPCHAIN_USAGE_MUTABLE_FORMAT;
	}

	return usage;
}

XrResult
oxr_create_swapchain(struct oxr_logger *log,
                     struct oxr_session *sess,
                     const XrSwapchainCreateInfo *createInfo,
                     struct oxr_swapchain **out_swapchain)
{
	struct xrt_swapchain *xsc = xrt_comp_create_swapchain(
	    sess->compositor, convert_create_flags(createInfo->createFlags),
	    convert_usage_bits(createInfo->usageFlags), createInfo->format,
	    createInfo->sampleCount, createInfo->width, createInfo->height,
	    createInfo->faceCount, createInfo->arraySize, createInfo->mipCount);

	if (xsc == NULL) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 " failed to create swapchain");
	}

	struct oxr_swapchain *sc = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, sc, OXR_XR_DEBUG_SWAPCHAIN,
	                              oxr_swapchain_destroy, &sess->handle);
	sc->sess = sess;
	sc->swapchain = xsc;
	sc->acquire_image = oxr_swapchain_acquire_image;
	sc->wait_image = oxr_swapchain_wait_image;
	sc->release_image = oxr_swapchain_release_image;
	sc->is_static = (createInfo->createFlags &
	                 XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) != 0;

	*out_swapchain = sc;

	return XR_SUCCESS;
}
