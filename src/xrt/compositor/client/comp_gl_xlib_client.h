// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGL on Xlib client side glue to compositor header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#pragma once

#include "xrt/xrt_gfx_xlib.h"
#include "client/comp_gl_client.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @class client_gl_xlib_compositor
 * A client facing xlib OpenGL base compositor.
 *
 * @ingroup comp_client
 * @extends client_gl_compositor
 */
struct client_gl_xlib_compositor
{
	//! OpenGL compositor wrapper base.
	struct client_gl_compositor base;
};

/*!
 * Create a new client_gl_xlib_compositor.
 *
 * @public @memberof client_gl_xlib_compositor
 * @relatesalso xrt_compositor_native
 */
struct client_gl_xlib_compositor *
client_gl_xlib_compositor_create(struct xrt_compositor_native *xcn,
                                 Display *xDisplay,
                                 uint32_t visualid,
                                 GLXFBConfig glxFBConfig,
                                 GLXDrawable glxDrawable,
                                 GLXContext glxContext);


#ifdef __cplusplus
}
#endif
