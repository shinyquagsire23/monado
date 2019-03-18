// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Xlib client side glue to compositor implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include <stdio.h>
#include <stdlib.h>

#include "xrt/xrt_gfx_xlib.h"

#include "client/comp_xlib_client.h"


static void
client_xlib_compositor_destroy(struct xrt_compositor *xc)
{
	struct client_xlib_compositor *c = client_xlib_compositor(xc);
	// Pipe down call into fd compositor.
	c->base.xcfd->base.destroy(&c->base.xcfd->base);
	free(c);
}

typedef void (*void_ptr_func)();

void_ptr_func
glXGetProcAddress(const char *procName);

struct client_xlib_compositor *
client_xlib_compositor_create(struct xrt_compositor_fd *xcfd,
                              Display *xDisplay,
                              uint32_t visualid,
                              GLXFBConfig glxFBConfig,
                              GLXDrawable glxDrawable,
                              GLXContext glxContext)
{
	struct client_xlib_compositor *c =
	    calloc(1, sizeof(struct client_xlib_compositor));

	if (!client_gl_compositor_init(&c->base, xcfd, glXGetProcAddress)) {
		free(c);
		return NULL;
	}

	c->base.base.base.destroy = client_xlib_compositor_destroy;

	return c;
}
