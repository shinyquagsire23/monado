// Copyright 2019-2022, Collabora, Ltd.
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
#include "util/u_logging.h"

#include "xrt/xrt_gfx_xlib.h"

#include "client/comp_gl_xlib_client.h"
#include "client/comp_gl_memobj_swapchain.h"

#include "ogl/ogl_api.h"
#include "ogl/glx_api.h"

/*
 *
 * OpenGL context helper.
 *
 */

static inline bool
context_matches(const struct client_gl_context *a, const struct client_gl_context *b)
{
	return a->ctx == b->ctx && a->draw == b->draw && a->read == b->read && a->dpy == b->dpy;
}

static inline void
context_save_current(struct client_gl_context *current_ctx)
{
	current_ctx->dpy = glXGetCurrentDisplay();
	current_ctx->ctx = glXGetCurrentContext();
	current_ctx->read = glXGetCurrentDrawable();
	current_ctx->draw = glXGetCurrentReadDrawable();
}

static inline bool
context_make_current(const struct client_gl_context *ctx_to_make_current)
{
	if (glXMakeContextCurrent(ctx_to_make_current->dpy, ctx_to_make_current->draw, ctx_to_make_current->read,
	                          ctx_to_make_current->ctx)) {
		return true;
	}
	return false;
}

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

	client_gl_compositor_close(&c->base);

	free(c);
}

static xrt_result_t
client_gl_context_begin_locked(struct xrt_compositor *xc, enum client_gl_context_reason reason)
{
	struct client_gl_xlib_compositor *c = client_gl_xlib_compositor(xc);

	struct client_gl_context *app_ctx = &c->app_context;

	context_save_current(&c->temp_context);

	bool need_make_current = !context_matches(&c->temp_context, app_ctx);

	U_LOG_T("GL Context begin: need makeCurrent: %d (current %p -> app %p)", need_make_current,
	        (void *)c->temp_context.ctx, (void *)app_ctx->ctx);

	if (need_make_current && !context_make_current(app_ctx)) {
		U_LOG_E("Failed to make GLX context current");
		// No need to restore on failure.
		return XRT_ERROR_OPENGL;
	}

	return XRT_SUCCESS;
}

static void
client_gl_context_end_locked(struct xrt_compositor *xc, enum client_gl_context_reason reason)
{
	struct client_gl_xlib_compositor *c = client_gl_xlib_compositor(xc);

	struct client_gl_context *app_ctx = &c->app_context;

	struct client_gl_context *current_glx_context = &c->temp_context;

	bool need_make_current = !context_matches(&c->temp_context, app_ctx);

	U_LOG_T("GL Context end: need makeCurrent: %d (app %p -> current %p)", need_make_current, (void *)app_ctx->ctx,
	        (void *)c->temp_context.ctx);

	if (need_make_current && !context_make_current(current_glx_context)) {
		U_LOG_E("Failed to make old GLX context current! (%p, %#lx, %#lx, %p)",
		        (void *)current_glx_context->dpy, (unsigned long)current_glx_context->draw,
		        (unsigned long)current_glx_context->read, (void *)current_glx_context->ctx);
		// fall through to os_mutex_unlock even if we didn't succeed in restoring the context
	}
}

typedef void (*void_ptr_func)(void);

#ifdef __cplusplus
extern "C"
#endif
    void_ptr_func
    glXGetProcAddress(const char *procName);

struct client_gl_xlib_compositor *
client_gl_xlib_compositor_create(struct xrt_compositor_native *xcn,
                                 Display *xDisplay,
                                 uint32_t visualid,
                                 GLXFBConfig glxFBConfig,
                                 GLXDrawable glxDrawable,
                                 GLXContext glxContext)
{
	// We're not using any GLX extensions so screen number is irrelevant.
	gladLoadGLX(xDisplay, 0, glXGetProcAddress);

	// Save old GLX context.
	struct client_gl_context current_ctx;
	context_save_current(&current_ctx);

	// The context and drawables given from the app.
	struct client_gl_context app_ctx = {
	    .dpy = xDisplay,
	    .ctx = glxContext,
	    .draw = glxDrawable,
	    .read = glxDrawable,
	};


	bool need_make_current = !context_matches(&current_ctx, &app_ctx);

	U_LOG_T("GL Compositor create: need makeCurrent: %d (current %p -> app %p)", need_make_current,
	        (void *)current_ctx.ctx, (void *)app_ctx.ctx);

	if (need_make_current && !context_make_current(&app_ctx)) {
		U_LOG_E("Failed to make GLX context current");
		// No need to restore on failure.
		return NULL;
	}

	gladLoadGL(glXGetProcAddress);


	U_LOG_T("GL Compositor create: need makeCurrent: %d (app %p -> current %p)", need_make_current,
	        (void *)app_ctx.ctx, (void *)current_ctx.ctx);

	if (need_make_current && !context_make_current(&current_ctx)) {
		U_LOG_E("Failed to make old GLX context current! (%p, %#lx, %#lx, %p)", (void *)current_ctx.dpy,
		        (unsigned long)current_ctx.draw, (unsigned long)current_ctx.read, (void *)current_ctx.ctx);
	}

#define CHECK_REQUIRED_EXTENSION(EXT)                                                                                  \
	do {                                                                                                           \
		if (!GLAD_##EXT) {                                                                                     \
			U_LOG_E("%s - Required OpenGL extension " #EXT " not available", __func__);                    \
			return NULL;                                                                                   \
		}                                                                                                      \
	} while (0)

	CHECK_REQUIRED_EXTENSION(GL_EXT_memory_object);
#ifdef XRT_OS_LINUX
	CHECK_REQUIRED_EXTENSION(GL_EXT_memory_object_fd);
#endif

#undef CHECK_REQUIRED_EXTENSION

	struct client_gl_xlib_compositor *c = U_TYPED_CALLOC(struct client_gl_xlib_compositor);

	// Move the app context to the struct.
	c->app_context = app_ctx;

	if (!client_gl_compositor_init(            //
	        &c->base,                          //
	        xcn,                               //
	        client_gl_context_begin_locked,    //
	        client_gl_context_end_locked,      //
	        client_gl_memobj_swapchain_create, //
	        NULL)) {                           //
		free(c);
		return NULL;
	}

	c->base.base.base.destroy = client_gl_xlib_compositor_destroy;

	return c;
}
