// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds OpenGL swapchain related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#include <stdlib.h>

#include "xrt/xrt_gfx_xlib.h"
#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"


static XrResult
oxr_swapchain_gl_destroy(struct oxr_logger *log, struct oxr_swapchain *sc)
{
	if (sc->acquired_index >= 0) {
		sc->release_image(log, sc, NULL);
	}

	if (sc->swapchain != NULL) {
		sc->swapchain->destroy(sc->swapchain);
		sc->swapchain = NULL;
	}

	return XR_SUCCESS;
}

static XrResult
oxr_swapchain_gl_enumerate_images(struct oxr_logger *log,
                                  struct oxr_swapchain *sc,
                                  uint32_t count,
                                  XrSwapchainImageBaseHeader *images)
{
	struct xrt_swapchain_gl *xsc = (struct xrt_swapchain_gl *)sc->swapchain;
	XrSwapchainImageOpenGLKHR *gl_imgs =
	    (XrSwapchainImageOpenGLKHR *)images;

	for (uint32_t i = 0; i < count; i++) {
		gl_imgs[i].image = xsc->images[i];
	}

	return oxr_session_success_result(sc->sess);
}

XrResult
oxr_swapchain_gl_create(struct oxr_logger *log,
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

	sc->destroy = oxr_swapchain_gl_destroy;
	sc->enumerate_images = oxr_swapchain_gl_enumerate_images;

	*out_swapchain = sc;

	return XR_SUCCESS;
}
