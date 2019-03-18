// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to Xlib client side glue code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp
 */

#include <stdio.h>
#include <stdlib.h>

#include "xrt/xrt_gfx_xlib.h"

#include "main/comp_client_interface.h"
#include "client/comp_xlib_client.h"


struct xrt_compositor_gl *
xrt_gfx_provider_create_gl_xlib(struct xrt_device *xdev,
                                Display *xDisplay,
                                uint32_t visualid,
                                GLXFBConfig glxFBConfig,
                                GLXDrawable glxDrawable,
                                GLXContext glxContext)
{
	struct xrt_compositor_fd *xcfd = comp_compositor_create(xdev, true);
	if (xcfd == NULL) {
		return NULL;
	}

	struct client_xlib_compositor *xcc = client_xlib_compositor_create(
	    xcfd, xDisplay, visualid, glxFBConfig, glxDrawable, glxContext);

	return &xcc->base.base;
}
