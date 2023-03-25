// Copyright 2018-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds swapchain related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "util/u_debug.h"
#include "util/u_misc.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_handle.h"
#include "oxr_swapchain_common.h"
#include "oxr_xret.h"


/*
 *
 * Conversion functions.
 *
 */

static enum xrt_swapchain_create_flags
convert_create_flags(XrSwapchainCreateFlags xr_flags)
{
	enum xrt_swapchain_create_flags flags = 0;

	if ((xr_flags & XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT) != 0) {
		flags |= XRT_SWAPCHAIN_CREATE_PROTECTED_CONTENT;
	}
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
	// aliased to XR_SWAPCHAIN_USAGE_INPUT_ATTACHMENT_BIT_MND
	if ((xr_usage & XR_SWAPCHAIN_USAGE_INPUT_ATTACHMENT_BIT_KHR) != 0) {
		usage |= XRT_SWAPCHAIN_USAGE_INPUT_ATTACHMENT;
	}

	return usage;
}


/*
 *
 * Internal API functions.
 *
 */

static XrResult
acquire_image(struct oxr_logger *log,
              struct oxr_swapchain *sc,
              const XrSwapchainImageAcquireInfo *acquireInfo,
              uint32_t *out_index)
{
	CHECK_OXR_RET(oxr_swapchain_common_acquire(log, sc, out_index));

	return oxr_session_success_result(sc->sess);
}

static XrResult
implicit_wait_image(struct oxr_logger *log, struct oxr_swapchain *sc, const XrSwapchainImageWaitInfo *waitInfo)
{
	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;
	xrt_result_t xret;

	CHECK_OXR_RET(oxr_swapchain_verify_wait_state(log, sc));
	CHECK_OXR_RET(oxr_swapchain_common_wait(log, sc, waitInfo->timeout));

	// Check and grab the index.
	if (sc->inflight.index < 0) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Invalid state: sc->inflight.index < 0");
	}
	uint32_t index = (uint32_t)sc->inflight.index;

	// Okay to transition here for all APIs except Vulkan, who has it's own implementation of this function.
	xret = xrt_swapchain_barrier_image(xsc, XRT_BARRIER_TO_COMP, index);
	OXR_CHECK_XRET(log, sc->sess, xret, xrt_swapchain_barrier_image);

	return oxr_session_success_result(sc->sess);
}

static XrResult
implicit_release_image(struct oxr_logger *log, struct oxr_swapchain *sc, const XrSwapchainImageReleaseInfo *releaseInfo)
{
	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;
	xrt_result_t xret;

	// Error checking.
	if (!sc->inflight.yes) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID, "No swapchain images waited on");
	}

	// Check and grab the index.
	if (sc->inflight.index < 0) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Invalid state: sc->inflight.index < 0");
	}
	uint32_t index = (uint32_t)sc->inflight.index;

	if (sc->images[index].state != OXR_IMAGE_STATE_WAITED) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID, "No swapchain images waited on");
	}

	// Need to do a automatic transition here.
	xret = xrt_swapchain_barrier_image(xsc, XRT_BARRIER_TO_COMP, index);
	OXR_CHECK_XRET(log, sc->sess, xret, xrt_swapchain_barrier_image);

	CHECK_OXR_RET(oxr_swapchain_common_release(log, sc));

	return oxr_session_success_result(sc->sess);
}

static XrResult
destroy(struct oxr_logger *log, struct oxr_swapchain *sc)
{
	// It is not safe to do transitions here for some Graphics APIs, and
	// the ipc layer has to be robust enough to handle a disconnect.

	// Drop our reference, does NULL checking.
	xrt_swapchain_reference(&sc->swapchain, NULL);

	return XR_SUCCESS;
}


/*
 *
 * Handle function.
 *
 */

static XrResult
destroy_handle(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	struct oxr_swapchain *sc = (struct oxr_swapchain *)hb;

	XrResult ret = sc->destroy(log, sc);
	free(sc);
	return ret;
}


/*
 *
 * 'Exported' functions.
 *
 */

XrResult
oxr_swapchain_common_acquire(struct oxr_logger *log, struct oxr_swapchain *sc, uint32_t *out_index)
{
	uint32_t index;

	if (sc->acquired.num >= sc->swapchain->image_count) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID, "All images have been acquired");
	}

	if (sc->is_static && (sc->released.yes || sc->images[0].state != OXR_IMAGE_STATE_READY)) {
		return oxr_error(log, XR_ERROR_CALL_ORDER_INVALID, "Can only acquire once on a static swapchain");
	}

	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;

	xrt_result_t xret = xrt_swapchain_acquire_image(xsc, &index);
	OXR_CHECK_XRET(log, sc->sess, xret, xrt_swapchain_acquire_image);

	if (sc->images[index].state != OXR_IMAGE_STATE_READY) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Internal xrt_swapchain_acquire_image call returned non-ready image.");
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

	return XR_SUCCESS;
}

XrResult
oxr_swapchain_common_wait(struct oxr_logger *log, struct oxr_swapchain *sc, XrDuration timeout)
{
	uint32_t index;
	u_index_fifo_pop(&sc->acquired.fifo, &index);

	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;

	xrt_result_t xret = xrt_swapchain_wait_image(xsc, timeout, index);
	OXR_CHECK_XRET(log, sc->sess, xret, xrt_swapchain_wait_image);

	// The app can only wait on one image.
	sc->inflight.yes = true;
	sc->inflight.index = index;
	sc->images[index].state = OXR_IMAGE_STATE_WAITED;

	return XR_SUCCESS;
}

XrResult
oxr_swapchain_common_release(struct oxr_logger *log, struct oxr_swapchain *sc)
{
	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;

	// Check and grab the index.
	if (sc->inflight.index < 0) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Invalid state: sc->inflight.index < 0");
	}
	uint32_t index = (uint32_t)sc->inflight.index;

	// Clear inflight.
	sc->inflight.yes = false;
	sc->inflight.index = -1;

	xrt_result_t xret = xrt_swapchain_release_image(xsc, index);
	OXR_CHECK_XRET(log, sc->sess, xret, xrt_swapchain_release_image);

	// Only decerement here.
	sc->acquired.num--;

	// Overwrite the old released image, with new.
	sc->released.yes = true;
	sc->released.index = index;
	sc->images[index].state = OXR_IMAGE_STATE_READY;

	return XR_SUCCESS;
}

XrResult
oxr_swapchain_common_create(struct oxr_logger *log,
                            struct oxr_session *sess,
                            const XrSwapchainCreateInfo *createInfo,
                            struct oxr_swapchain **out_swapchain)
{
	xrt_result_t xret = XRT_SUCCESS;

	struct xrt_swapchain_create_info info;
	info.create = convert_create_flags(createInfo->createFlags);
	info.bits = convert_usage_bits(createInfo->usageFlags);
	info.format = createInfo->format;
	info.sample_count = createInfo->sampleCount;
	info.width = createInfo->width;
	info.height = createInfo->height;
	info.face_count = createInfo->faceCount;
	info.array_size = createInfo->arraySize;
	info.mip_count = createInfo->mipCount;

	struct xrt_swapchain *xsc = NULL; // Has to be NULL.
	xret = xrt_comp_create_swapchain(sess->compositor, &info, &xsc);
	if (xret == XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED) {
		return oxr_error(log, XR_ERROR_FEATURE_UNSUPPORTED,
		                 "Specified swapchain creation flag is valid, "
		                 "but not supported");
	}
	if (xret == XRT_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED) {
		return oxr_error(log, XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED,
		                 "Specified swapchain format is not supported");
	}
	if (xret != XRT_SUCCESS) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Failed to create swapchain");
	}
	assert(xsc != NULL);

	struct oxr_swapchain *sc = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, sc, OXR_XR_DEBUG_SWAPCHAIN, destroy_handle, &sess->handle);
	sc->sess = sess;
	sc->swapchain = xsc;
	sc->width = createInfo->width;
	sc->height = createInfo->height;
	sc->array_layer_count = createInfo->arraySize;
	sc->face_count = createInfo->faceCount;
	sc->is_static = (createInfo->createFlags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) != 0;

	// Functions.
	sc->wait_image = implicit_wait_image;
	sc->release_image = implicit_release_image;
	sc->acquire_image = acquire_image;
	sc->destroy = destroy;

	*out_swapchain = sc;

	return XR_SUCCESS;
}
