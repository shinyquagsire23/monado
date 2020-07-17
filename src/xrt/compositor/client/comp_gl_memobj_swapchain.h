// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGL client side glue using memory objects - header.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#pragma once

#include "comp_gl_client.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @class client_gl_memobj_swapchain
 *
 * Wraps the real compositor swapchain providing a OpenGL based interface.
 *
 * Almost a one to one mapping to a OpenXR swapchain.
 *
 * @ingroup comp_client
 * @implements xrt_swapchain_gl
 */
struct client_gl_memobj_swapchain
{
	struct client_gl_swapchain base;

	// GLuint
	unsigned int memory[XRT_MAX_SWAPCHAIN_IMAGES];
};

/*!
 * Create a swapchain, belonging to a client_gl_compositor, that uses
 * GL_memory_object and related extensions to access the native buffer.
 *
 * This is most commonly used on desktop OpenGL.
 *
 * @see client_gl_swapchain_create_func, client_gl_compositor_init
 */
struct xrt_swapchain *
client_gl_memobj_swapchain_create(struct xrt_compositor *xc,
                                  const struct xrt_swapchain_create_info *info,
                                  struct xrt_swapchain_native *xscn,
                                  struct client_gl_swapchain **out_sc);


#ifdef __cplusplus
}
#endif
