// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds Vulkan swapchain related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_swapchain_common.h"
#include "oxr_xret.h"
#include <stdint.h>

#define WAIT_IN_ACQUIRE (true)


static XrResult
vk_implicit_acquire_image(struct oxr_logger *log,
                          struct oxr_swapchain *sc,
                          const XrSwapchainImageAcquireInfo *acquireInfo,
                          uint32_t *out_index)
{
	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;
	xrt_result_t xret;

	uint32_t index;
	CHECK_OXR_RET(oxr_swapchain_common_acquire(log, sc, &index));

	/*
	 * We have to wait here in order to be fully conformat to the Vulkan
	 * spec, it stats that the compositor has to have completed the GPU
	 * commands to transfer the image to an external queue in order for us
	 * to be able to insert our transition.
	 */
	if (WAIT_IN_ACQUIRE) {
		xret = xrt_swapchain_wait_image(xsc, XR_INFINITE_DURATION, index);
		OXR_CHECK_XRET(log, sc->sess, xret, xrt_swapchain_wait_image);
	}

	/*
	 * The non-explicit transition versions of XR_vulkan_enable[_2] states
	 * that we can only use the queue in xrAcquireSwapchainImage so must be
	 * done here.
	 */
	xret = xrt_swapchain_barrier_image(xsc, XRT_BARRIER_TO_APP, index);
	OXR_CHECK_XRET(log, sc->sess, xret, xrt_swapchain_barrier_image);

	*out_index = index;

	return oxr_session_success_result(sc->sess);
}

static XrResult
vk_implicit_wait_image(struct oxr_logger *log, struct oxr_swapchain *sc, const XrSwapchainImageWaitInfo *waitInfo)
{
	CHECK_OXR_RET(oxr_swapchain_verify_wait_state(log, sc));

	uint32_t index = UINT32_MAX;
	if (u_index_fifo_pop(&sc->acquired.fifo, &index) != 0) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "u_index_fifo_pop: failed!");
	}
	assert(index < INT32_MAX);

	struct xrt_swapchain *xsc = (struct xrt_swapchain *)sc->swapchain;

	if (!WAIT_IN_ACQUIRE) {
		XrDuration timeout = waitInfo->timeout;

		// We have already waited in acquire.
		xrt_result_t xret = xrt_swapchain_wait_image(xsc, timeout, index);
		OXR_CHECK_XRET(log, sc->sess, xret, xrt_swapchain_wait_image);
	}

	// The app can only wait on one image.
	sc->inflight.yes = true;
	sc->inflight.index = (int)index;
	sc->images[index].state = OXR_IMAGE_STATE_WAITED;

	return XR_SUCCESS;
}

static XrResult
vk_enumerate_images(struct oxr_logger *log,
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

	ret = oxr_swapchain_common_create(log, sess, createInfo, &sc);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	// Set our API specific function(s).
	sc->enumerate_images = vk_enumerate_images;
	sc->acquire_image = vk_implicit_acquire_image;
	sc->wait_image = vk_implicit_wait_image;

	*out_swapchain = sc;

	return XR_SUCCESS;
}
