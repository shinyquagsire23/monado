// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Swapchain entrypoints for the OpenXR state tracker.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_api
 */

#include "xrt/xrt_compiler.h"

#include "util/u_debug.h"
#include "util/u_trace_marker.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"

#include "oxr_api_funcs.h"
#include "oxr_api_verify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


XrResult
oxr_xrEnumerateSwapchainFormats(XrSession session,
                                uint32_t formatCapacityInput,
                                uint32_t *formatCountOutput,
                                int64_t *formats)
{
	OXR_TRACE_MARKER();

	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrEnumerateSwapchainFormats");

	return oxr_session_enumerate_formats(&log, sess, formatCapacityInput, formatCountOutput, formats);
}

XrResult
oxr_xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo *createInfo, XrSwapchain *out_swapchain)
{
	OXR_TRACE_MARKER();

	XrResult ret;
	struct oxr_session *sess;
	struct oxr_swapchain *sc;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrCreateSwapchain");
	if (sess->compositor == NULL) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE, "Is illegal in headless sessions");
	}
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, createInfo, XR_TYPE_SWAPCHAIN_CREATE_INFO);
	OXR_VERIFY_ARG_NOT_NULL(&log, out_swapchain);

	// Save people from shooting themselves in the foot.
	OXR_VERIFY_ARG_NOT_ZERO(&log, createInfo->arraySize);
	OXR_VERIFY_ARG_NOT_ZERO(&log, createInfo->width);
	OXR_VERIFY_ARG_NOT_ZERO(&log, createInfo->height);

	if (createInfo->faceCount != 1 && createInfo->faceCount != 6) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE, "faceCount must be 1 or 6");
	}

	// Short hand.
	struct oxr_instance *inst = sess->sys->inst;

	XrSwapchainUsageFlags flags = 0;
	flags |= XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	flags |= XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	flags |= XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT;
	flags |= XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
	flags |= XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	flags |= XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
	flags |= XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
	if (inst->extensions.MND_swapchain_usage_input_attachment_bit ||
	    inst->extensions.KHR_swapchain_usage_input_attachment_bit) {
		// aliased to XR_SWAPCHAIN_USAGE_INPUT_ATTACHMENT_BIT_MND
		flags |= XR_SWAPCHAIN_USAGE_INPUT_ATTACHMENT_BIT_KHR;
	}

	if ((createInfo->usageFlags & ~flags) != 0) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE,
		                 "(createInfo->usageFlags == 0x%04" PRIx64 ") contains invalid flags",
		                 createInfo->usageFlags);
	}
	bool format_supported = false;
	struct xrt_compositor *c = sess->compositor;
	for (uint32_t i = 0; i < c->info.format_count; i++) {
		if (c->info.formats[i] == createInfo->format) {
			format_supported = true;
			break;
		}
	}

	if (!format_supported) {
		return oxr_error(&log, XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED,
		                 "(createInfo->format == 0x%04" PRIx64 ") is not supported", createInfo->format);
	}

	ret = sess->create_swapchain(&log, sess, createInfo, &sc);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*out_swapchain = oxr_swapchain_to_openxr(sc);

	return oxr_session_success_result(sess);
}

XrResult
oxr_xrDestroySwapchain(XrSwapchain swapchain)
{
	OXR_TRACE_MARKER();

	struct oxr_swapchain *sc;
	struct oxr_logger log;
	OXR_VERIFY_SWAPCHAIN_AND_INIT_LOG(&log, swapchain, sc, "xrDestroySwapchain");

	return oxr_handle_destroy(&log, &sc->handle);
}

XrResult
oxr_xrEnumerateSwapchainImages(XrSwapchain swapchain,
                               uint32_t imageCapacityInput,
                               uint32_t *imageCountOutput,
                               XrSwapchainImageBaseHeader *images)
{
	OXR_TRACE_MARKER();

	struct oxr_swapchain *sc;
	struct oxr_logger log;
	OXR_VERIFY_SWAPCHAIN_AND_INIT_LOG(&log, swapchain, sc, "xrEnumerateSwapchainImages");
	struct xrt_swapchain *xsc = sc->swapchain;

	if (imageCountOutput != NULL) {
		*imageCountOutput = xsc->image_count;
	}
	if (imageCapacityInput == 0) {
		return XR_SUCCESS;
	}
	if (imageCapacityInput < xsc->image_count) {
		return oxr_error(&log, XR_ERROR_SIZE_INSUFFICIENT, "(imageCapacityInput == %u)", imageCapacityInput);
	}

	return sc->enumerate_images(&log, sc, xsc->image_count, images);
}

XrResult
oxr_xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo *acquireInfo, uint32_t *index)
{
	OXR_TRACE_MARKER();

	struct oxr_swapchain *sc;
	struct oxr_logger log;
	OXR_VERIFY_SWAPCHAIN_AND_INIT_LOG(&log, swapchain, sc, "xrAcquireSwapchainImage");
	OXR_VERIFY_ARG_TYPE_CAN_BE_NULL(&log, acquireInfo, XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
	OXR_VERIFY_ARG_NOT_NULL(&log, index);

	return sc->acquire_image(&log, sc, acquireInfo, index);
}

XrResult
oxr_xrWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo *waitInfo)
{
	OXR_TRACE_MARKER();

	struct oxr_swapchain *sc;
	struct oxr_logger log;
	OXR_VERIFY_SWAPCHAIN_AND_INIT_LOG(&log, swapchain, sc, "xrWaitSwapchainImage");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, waitInfo, XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);

	return sc->wait_image(&log, sc, waitInfo);
}

XrResult
oxr_xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo *releaseInfo)
{
	OXR_TRACE_MARKER();

	struct oxr_swapchain *sc;
	struct oxr_logger log;
	OXR_VERIFY_SWAPCHAIN_AND_INIT_LOG(&log, swapchain, sc, "xrReleaseSwapchainImage");
	OXR_VERIFY_ARG_TYPE_CAN_BE_NULL(&log, releaseInfo, XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);

	return sc->release_image(&log, sc, releaseInfo);
}
