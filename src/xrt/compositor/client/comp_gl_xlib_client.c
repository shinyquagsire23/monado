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

#include "util/u_misc.h"

#include "xrt/xrt_gfx_xlib.h"

#include "client/comp_gl_xlib_client.h"


/*!
 * Down-cast helper.
 *
 * @private @memberof client_gl_xlib_compositor
 */
static inline struct client_gl_xlib_compositor *
client_gl_xlib_compositor(struct xrt_compositor *xc)
{
	return (struct client_gl_xlib_compositor *)xc;
}

static void
client_gl_xlib_compositor_destroy(struct xrt_compositor *xc)
{
	struct client_gl_xlib_compositor *c = client_gl_xlib_compositor(xc);
	// Pipe down call into fd compositor.
	xrt_comp_fd_destroy(&c->base.xcfd);
	free(c);
}

typedef void (*void_ptr_func)();

#ifdef __cplusplus
extern "C"
#endif
    void_ptr_func
    glXGetProcAddress(const char *procName);

struct client_gl_xlib_compositor *
client_gl_xlib_compositor_create(struct xrt_compositor_fd *xcfd,
                                 Display *xDisplay,
                                 uint32_t visualid,
                                 GLXFBConfig glxFBConfig,
                                 GLXDrawable glxDrawable,
                                 GLXContext glxContext)
{
	struct client_gl_xlib_compositor *c =
	    U_TYPED_CALLOC(struct client_gl_xlib_compositor);

	if (!client_gl_compositor_init(&c->base, xcfd, glXGetProcAddress)) {
		free(c);
		return NULL;
	}

	c->base.base.base.destroy = client_gl_xlib_compositor_destroy;

	return c;
}
