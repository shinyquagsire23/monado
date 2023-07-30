// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds OpenGL swapchain related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_swapchain_common.h"

#include <assert.h>
#include <stdlib.h>


#if defined(XR_USE_GRAPHICS_API_OPENGL) || defined(XR_USE_GRAPHICS_API_OPENGL_ES)


#if defined(XR_USE_GRAPHICS_API_OPENGL)
static XrResult
gl_enumerate_images_gl(struct oxr_logger *log,
                       struct oxr_swapchain *sc,
                       uint32_t count,
                       XrSwapchainImageOpenGLKHR *images)
{
	struct xrt_swapchain_gl *xsc = (struct xrt_swapchain_gl *)sc->swapchain;
	for (uint32_t i = 0; i < count; i++) {
		if (images[i].type != images[0].type) {
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE, "Images array contains mixed types");
		}
		images[i].image = xsc->images[i];
	}

	return oxr_session_success_result(sc->sess);
}

#endif // XR_USE_GRAPHICS_API_OPENGL

#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
static XrResult
gl_enumerate_images_gles(struct oxr_logger *log,
                         struct oxr_swapchain *sc,
                         uint32_t count,
                         XrSwapchainImageOpenGLESKHR *images)
{
	struct xrt_swapchain_gl *xsc = (struct xrt_swapchain_gl *)sc->swapchain;
	for (uint32_t i = 0; i < count; i++) {
		if (images[i].type != images[0].type) {
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE, "Images array contains mixed types");
		}
		images[i].image = xsc->images[i];
	}

	return oxr_session_success_result(sc->sess);
}
#endif // XR_USE_GRAPHICS_API_OPENGL_ES

static XrResult
gl_enumerate_images(struct oxr_logger *log,
                    struct oxr_swapchain *sc,
                    uint32_t count,
                    XrSwapchainImageBaseHeader *images)
{
	assert(count > 0);
	switch (images[0].type) {
#if defined(XR_USE_GRAPHICS_API_OPENGL)
	case XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR:
		return gl_enumerate_images_gl(log, sc, count, (XrSwapchainImageOpenGLKHR *)images);
#endif // XR_USE_GRAPHICS_API_OPENGL
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
	case XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR:
		return gl_enumerate_images_gles(log, sc, count, (XrSwapchainImageOpenGLESKHR *)images);
#endif // XR_USE_GRAPHICS_API_OPENGL_ES
	default: return oxr_error(log, XR_ERROR_VALIDATION_FAILURE, "Unsupported XrSwapchainImageBaseHeader type");
	}
}

XrResult
oxr_swapchain_gl_create(struct oxr_logger *log,
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
	sc->enumerate_images = gl_enumerate_images;

	*out_swapchain = sc;

	return XR_SUCCESS;
}
#endif // XR_USE_GRAPHICS_API_OPENGL || XR_USE_GRAPHICS_API_OPENGL_ES
