// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to OpenGL Xlib client side code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include <stdio.h>
#include <stdlib.h>

#include "xrt/xrt_gfx_xlib.h"

#include "client/comp_gl_xlib_client.h"


struct xrt_compositor_gl *
xrt_gfx_provider_create_gl_xlib(struct xrt_compositor_native *xcn,
                                Display *xDisplay,
                                uint32_t visualid,
                                GLXFBConfig glxFBConfig,
                                GLXDrawable glxDrawable,
                                GLXContext glxContext)
{
	struct client_gl_xlib_compositor *xcc =
	    client_gl_xlib_compositor_create(xcn, xDisplay, visualid, glxFBConfig, glxDrawable, glxContext);

	return &xcc->base.base;
}
