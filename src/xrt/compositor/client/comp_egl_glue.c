// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to EGL client side glue code.
 * @author Drew DeVault <sir@cmpwn.com>
 * @author Simon Ser <contact@emersion.fr>
 * @ingroup comp
 */

#define EGL_EGL_PROTOTYPES 0
#define EGL_NO_X11
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>

#include "client/comp_gl_client.h"
#include "util/u_misc.h"
#include "xrt/xrt_gfx_egl.h"


// Not forward declared by mesa
typedef EGLBoolean EGLAPIENTRY (*PFNEGLMAKECURRENTPROC)(EGLDisplay dpy,
                                                        EGLSurface draw,
                                                        EGLSurface read,
                                                        EGLContext ctx);

static void
client_egl_compositor_destroy(struct xrt_compositor *xc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into fd compositor.
	c->xcfd->base.destroy(&c->xcfd->base);
	free(c);
}

struct xrt_compositor_gl *
xrt_gfx_provider_create_gl_egl(struct xrt_compositor_fd *xcfd,
                               EGLDisplay display,
                               EGLConfig config,
                               EGLContext context,
                               PFNEGLGETPROCADDRESSPROC getProcAddress)
{
	PFNEGLMAKECURRENTPROC eglMakeCurrent =
	    (PFNEGLMAKECURRENTPROC)getProcAddress("eglMakeCurrent");
	if (!eglMakeCurrent) {
		/* TODO: sort out logging here */
		fprintf(stderr, "getProcAddress(eglMakeCurrent) failed\n");
		return NULL;
	}

	if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
		fprintf(stderr, "Failed to make EGL context current\n");
		return NULL;
	}

	struct client_gl_compositor *c =
	    U_TYPED_CALLOC(struct client_gl_compositor);
	if (!client_gl_compositor_init(c, xcfd, getProcAddress)) {
		free(c);
		fprintf(stderr, "Failed to initialize compositor\n");
		return NULL;
	}

	c->base.base.destroy = client_egl_compositor_destroy;
	return &c->base;
}
