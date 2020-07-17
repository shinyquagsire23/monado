// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to EGL client side glue code.
 * @author Drew DeVault <sir@cmpwn.com>
 * @author Simon Ser <contact@emersion.fr>
 * @ingroup comp_client
 */

#include <xrt/xrt_config_os.h>
#include <xrt/xrt_config_have.h>

#include "ogl/egl_api.h"
#include "ogl/ogl_api.h"

#include <stdio.h>
#include <stdlib.h>

#include "client/comp_gl_client.h"
#include "client/comp_gl_memobj_swapchain.h"
#include "client/comp_gl_eglimage_swapchain.h"
#include "util/u_misc.h"
#include "xrt/xrt_gfx_egl.h"


#ifndef XRT_HAVE_EGL
#error "This file shouldn't be compiled without EGL"
#endif

// Not forward declared by mesa
typedef EGLBoolean EGLAPIENTRY (*PFNEGLMAKECURRENTPROC)(EGLDisplay dpy,
                                                        EGLSurface draw,
                                                        EGLSurface read,
                                                        EGLContext ctx);

static void
client_egl_compositor_destroy(struct xrt_compositor *xc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into native compositor.
	xrt_comp_native_destroy(&c->xcn);
	free(c);
}

struct xrt_compositor_gl *
xrt_gfx_provider_create_gl_egl(struct xrt_compositor_native *xcn,
                               EGLDisplay display,
                               EGLConfig config,
                               EGLContext context,
                               PFNEGLGETPROCADDRESSPROC get_gl_procaddr)
{
#if defined(XRT_HAVE_OPENGL)
	gladLoadGL(get_gl_procaddr);
#elif defined(XRT_HAVE_OPENGLES)
	gladLoadGLES2(get_gl_procaddr);
#endif
	gladLoadEGL(display, get_gl_procaddr);

	if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
		fprintf(stderr, "Failed to make EGL context current\n");
		return NULL;
	}

	struct client_gl_compositor *c =
	    U_TYPED_CALLOC(struct client_gl_compositor);

#if defined(XRT_OS_ANDROID)
	client_gl_swapchain_create_func sc_create = NULL;
#else
	client_gl_swapchain_create_func sc_create =
	    client_gl_memobj_swapchain_create;
	if (!GLAD_GL_EXT_memory_object && GLAD_EGL_EXT_image_dma_buf_import) {
		sc_create = client_gl_eglimage_swapchain_create;
	}
#endif

	if (!client_gl_compositor_init(c, xcn, get_gl_procaddr, sc_create)) {

		free(c);
		fprintf(stderr, "Failed to initialize compositor\n");
		return NULL;
	}

	c->base.base.destroy = client_egl_compositor_destroy;
	return &c->base;
}
