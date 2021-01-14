// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGL client side glue using EGLImageKHR - header.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#pragma once

#include "comp_gl_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *EGLImage;
typedef void *EGLDisplay;

/*!
 * @class client_gl_eglimage_swapchain
 *
 * Wraps the real compositor swapchain providing a OpenGL based interface.
 *
 * Almost a one to one mapping to a OpenXR swapchain.
 *
 * @ingroup comp_client
 * @implements xrt_swapchain_gl
 */
struct client_gl_eglimage_swapchain
{
	struct client_gl_swapchain base;

	EGLDisplay display;

	EGLImage egl_images[XRT_MAX_SWAPCHAIN_IMAGES];
};

/*!
 * Create a swapchain, belonging to a client_gl_compositor, that uses
 * some way of producing an EGLImageKHR from the native buffer.
 *
 * This is used on Android and on desktop when the EGL extension is used.
 */
struct xrt_swapchain *
client_gl_eglimage_swapchain_create(struct xrt_compositor *xc,
                                    const struct xrt_swapchain_create_info *info,
                                    struct xrt_swapchain_native *xscn,
                                    struct client_gl_swapchain **out_sc);

#ifdef __cplusplus
}
#endif
