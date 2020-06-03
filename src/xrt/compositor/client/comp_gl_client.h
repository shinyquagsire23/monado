// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGL client side glue to compositor header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#pragma once

#include "xrt/xrt_compositor.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Structs
 *
 */

/*!
 * @class client_gl_swapchain
 *
 * Wraps the real compositor swapchain providing a OpenGL based interface.
 *
 * Almost a one to one mapping to a OpenXR swapchain.
 *
 * @ingroup comp_client
 * @implements xrt_swapchain_gl
 */
struct client_gl_swapchain
{
	struct xrt_swapchain_gl base;

	struct xrt_swapchain_fd *xscfd;
};

/*!
 * @class client_gl_compositor
 *
 * Wraps the real compositor providing a OpenGL based interface.
 *
 * @ingroup comp_client
 * @implements xrt_compositor_gl
 */
struct client_gl_compositor
{
	struct xrt_compositor_gl base;

	struct xrt_compositor_fd *xcfd;
};


/*
 *
 * Functions and helpers.
 *
 */


/*!
 * Down-cast helper.
 * @protected @memberof client_gl_compositor
 */
static inline struct client_gl_compositor *
client_gl_compositor(struct xrt_compositor *xc)
{
	return (struct client_gl_compositor *)xc;
}

typedef void (*client_gl_void_ptr_func)();

typedef client_gl_void_ptr_func (*client_gl_get_procaddr)(const char *name);

/*!
 * Fill in a client_gl_compositor and do common OpenGL sanity checking.
 *
 * OpenGL can have multiple backing window systems we have to interact with, so
 * there isn't just one unified OpenGL client constructor.
 *
 * Moves owenership of provided xcfd to the client_gl_compositor.
 *
 * @public @memberof client_gl_compositor
 * @relatesalso xrt_compositor_fd
 */
bool
client_gl_compositor_init(struct client_gl_compositor *c,
                          struct xrt_compositor_fd *xcfd,
                          client_gl_get_procaddr get_gl_procaddr);


#ifdef __cplusplus
}
#endif
