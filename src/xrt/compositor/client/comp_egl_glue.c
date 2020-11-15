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
#include "util/u_logging.h"
#include "util/u_debug.h"
#include "xrt/xrt_gfx_egl.h"
#include "xrt/xrt_handles.h"


#ifndef XRT_HAVE_EGL
#error "This file shouldn't be compiled without EGL"
#endif

static enum u_logging_level ll;

#define EGL_TRACE(...) U_LOG_IFL_T(ll, __VA_ARGS__)
#define EGL_DEBUG(...) U_LOG_IFL_D(ll, __VA_ARGS__)
#define EGL_INFO(...) U_LOG_IFL_I(ll, __VA_ARGS__)
#define EGL_WARN(...) U_LOG_IFL_W(ll, __VA_ARGS__)
#define EGL_ERROR(...) U_LOG_IFL_E(ll, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(egl_log, "EGL_LOG", U_LOGGING_WARN)

// Not forward declared by mesa
typedef EGLBoolean EGLAPIENTRY (*PFNEGLMAKECURRENTPROC)(EGLDisplay dpy,
                                                        EGLSurface draw,
                                                        EGLSurface read,
                                                        EGLContext ctx);


/*
 *
 * Old helper.
 *
 */

struct old_helper
{
	EGLDisplay dpy;
	EGLContext ctx;
	EGLSurface read, draw;
};

static inline struct old_helper
old_save(void)
{
	struct old_helper old = {
	    .dpy = eglGetCurrentDisplay(),
	    .ctx = eglGetCurrentContext(),
	    .read = eglGetCurrentSurface(EGL_READ),
	    .draw = eglGetCurrentSurface(EGL_DRAW),
	};

	return old;
}

static inline void
old_restore(struct old_helper *old)
{
	if (eglMakeCurrent(old->dpy, old->draw, old->read, old->ctx)) {
		return;
	}

	EGL_ERROR("Failed to make old EGL context current! (%p, %p, %p, %p)",
	          old->dpy, old->draw, old->read, old->ctx);
}


/*
 *
 * Functions.
 *
 */

static void
client_egl_compositor_destroy(struct xrt_compositor *xc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	free(c);
}

struct xrt_compositor_gl *
xrt_gfx_provider_create_gl_egl(struct xrt_compositor_native *xcn,
                               EGLDisplay display,
                               EGLConfig config,
                               EGLContext context,
                               PFNEGLGETPROCADDRESSPROC get_gl_procaddr)
{
	ll = debug_get_log_option_egl_log();

	gladLoadEGL(display, get_gl_procaddr);

	// Save old display, context and drawables.
	struct old_helper old = old_save();

	if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
		EGL_ERROR("Failed to make EGL context current");
		// No need to restore on failure.
		return NULL;
	}

	EGLint egl_client_type;
	if (!eglQueryContext(display, context, EGL_CONTEXT_CLIENT_TYPE,
	                     &egl_client_type)) {
		old_restore(&old);
		return NULL;
	}

	switch (egl_client_type) {
	case EGL_OPENGL_API:
#if defined(XRT_HAVE_OPENGL)
		gladLoadGL(get_gl_procaddr);
		break;
#else
		EGL_ERROR("OpenGL support not including in this runtime build");
		old_restore(&old);
		return NULL;
#endif

	case EGL_OPENGL_ES_API:
#if defined(XRT_HAVE_OPENGLES)
		gladLoadGLES2(get_gl_procaddr);
		break;
#else
		EGL_ERROR(
		    "OpenGL|ES support not including in this runtime build");
		old_restore(&old);
		return NULL;
#endif
	default: EGL_ERROR("Unsupported EGL client type"); return NULL;
	}

	struct client_gl_compositor *c =
	    U_TYPED_CALLOC(struct client_gl_compositor);

	client_gl_swapchain_create_func sc_create = NULL;

	EGL_INFO("Extension availability:");
#define DUMP_EXTENSION_STATUS(EXT)                                             \
	EGL_INFO("  - " #EXT ": %s", GLAD_##EXT ? "true" : "false")

	DUMP_EXTENSION_STATUS(GL_EXT_memory_object);
	DUMP_EXTENSION_STATUS(GL_EXT_memory_object_fd);
	DUMP_EXTENSION_STATUS(GL_EXT_memory_object_win32);
	DUMP_EXTENSION_STATUS(EGL_EXT_image_dma_buf_import);
	DUMP_EXTENSION_STATUS(GL_OES_EGL_image_external);
	DUMP_EXTENSION_STATUS(EGL_KHR_image);
	// DUMP_EXTENSION_STATUS(EGL_KHR_image_base);

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
	if (GLAD_GL_EXT_memory_object && GLAD_GL_EXT_memory_object_fd) {
		EGL_INFO("Using GL memory object swapchain implementation");
		sc_create = client_gl_memobj_swapchain_create;
	}
	if (sc_create == NULL && GLAD_EGL_EXT_image_dma_buf_import) {
		EGL_INFO("Using EGL_Image swapchain implementation");
		sc_create = client_gl_eglimage_swapchain_create;
	}
	if (sc_create == NULL) {
		free(c);
		EGL_ERROR(
		    "Could not find a required extension: need either "
		    "EGL_EXT_image_dma_buf_import or "
		    "GL_EXT_memory_object_fd");
		old_restore(&old);
		return NULL;
	}
#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	EGL_INFO(
	    "Using EGL_Image swapchain implementation with AHardwareBuffer");
	sc_create = client_gl_eglimage_swapchain_create;
#endif

	if (!client_gl_compositor_init(c, xcn, sc_create)) {
		free(c);
		fprintf(stderr, "Failed to initialize compositor\n");
		old_restore(&old);
		return NULL;
	}

	c->base.base.destroy = client_egl_compositor_destroy;
	old_restore(&old);
	return &c->base;
}
