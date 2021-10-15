// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Win32 client side glue to compositor implementation.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Milan Jaros <milan.jaros@vsb.cz>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include <stdio.h>
#include <stdlib.h>

#include "util/u_misc.h"
#include "util/u_logging.h"

#include "xrt/xrt_gfx_win32.h"

#include "client/comp_gl_win32_client.h"
#include "client/comp_gl_memobj_swapchain.h"

#include "ogl/ogl_api.h"
#include "ogl/wgl_api.h"

/*
 *
 * OpenGL context helper.
 *
 */

static inline bool
context_matches(const struct client_gl_context *a, const struct client_gl_context *b)
{
	return a->hDC == b->hDC && a->hGLRC == b->hGLRC;
}

static inline void
context_save_current(struct client_gl_context *current_ctx)
{
	current_ctx->hDC = wglGetCurrentDC();
	current_ctx->hGLRC = wglGetCurrentContext();
}

static inline bool
context_make_current(const struct client_gl_context *ctx_to_make_current)
{
	if (wglMakeCurrent(ctx_to_make_current->hDC, ctx_to_make_current->hGLRC)) {
		return true;
	}
	return false;
}

/*!
 * Down-cast helper.
 *
 * @private @memberof client_gl_win32_compositor
 */
static inline struct client_gl_win32_compositor *
client_gl_win32_compositor(struct xrt_compositor *xc)
{
	return (struct client_gl_win32_compositor *)xc;
}

static void
client_gl_win32_compositor_destroy(struct xrt_compositor *xc)
{
	struct client_gl_win32_compositor *c = client_gl_win32_compositor(xc);

	client_gl_compositor_close(&c->base);

	FreeLibrary(c->opengl);
	c->opengl = NULL;

	free(c);
}

static xrt_result_t
client_gl_context_begin(struct xrt_compositor *xc)
{
	struct client_gl_win32_compositor *c = client_gl_win32_compositor(xc);

	struct client_gl_context *app_ctx = &c->app_context;

	os_mutex_lock(&c->base.context_mutex);

	context_save_current(&c->temp_context);

	bool need_make_current = !context_matches(&c->temp_context, app_ctx);

	U_LOG_T("GL Context begin: need makeCurrent: %d (current %p -> app %p)", need_make_current,
	        (void *)c->temp_context.hGLRC, (void *)app_ctx->hGLRC);

	if (need_make_current && !context_make_current(app_ctx)) {
		os_mutex_unlock(&c->base.context_mutex);

		U_LOG_E("Failed to make WGL context current");
		// No need to restore on failure.
		return XRT_ERROR_OPENGL;
	}

	return XRT_SUCCESS;
}

static void
client_gl_context_end(struct xrt_compositor *xc)
{
	struct client_gl_win32_compositor *c = client_gl_win32_compositor(xc);

	struct client_gl_context *app_ctx = &c->app_context;

	struct client_gl_context *current_wgl_context = &c->temp_context;

	bool need_make_current = !context_matches(&c->temp_context, app_ctx);

	U_LOG_T("GL Context end: need makeCurrent: %d (app %p -> current %p)", need_make_current,
	        (void *)app_ctx->hGLRC, (void *)c->temp_context.hGLRC);

	if (need_make_current && !context_make_current(current_wgl_context)) {
		U_LOG_E("Failed to make old WGL context current!");
		// fall through to os_mutex_unlock even if we didn't succeed in restoring the context
	}

	os_mutex_unlock(&c->base.context_mutex);
}

static GLADapiproc
client_gl_get_proc_addr(void *userptr, const char *name)
{
	GLADapiproc ret = (GLADapiproc)wglGetProcAddress(name);
	if (ret == NULL) {
		ret = (GLADapiproc)GetProcAddress((HMODULE)userptr, name);
	}
	return ret;
}

struct client_gl_win32_compositor *
client_gl_win32_compositor_create(struct xrt_compositor_native *xcn, void *hDC, void *hGLRC)
{
	// Save old GLX context.
	struct client_gl_context current_ctx;
	context_save_current(&current_ctx);

	// The context and drawables given from the app.
	struct client_gl_context app_ctx = {
	    .hDC = hDC,
	    .hGLRC = hGLRC,
	};


	/*
	 * Make given context current if needed.
	 */

	bool need_make_current = !context_matches(&current_ctx, &app_ctx);

	if (need_make_current && !context_make_current(&app_ctx)) {
		U_LOG_E("Failed to make WGL context current");
		// No need to restore on failure.
		return NULL;
	}


	/*
	 * Load functions.
	 */

	HMODULE opengl = LoadLibraryW(L"opengl32.dll");

	int wgl_result = gladLoadWGLUserPtr(hDC, client_gl_get_proc_addr, opengl);
	int gl_result = gladLoadGLUserPtr(client_gl_get_proc_addr, opengl);

	if (glGetString != NULL) {
		U_LOG_D(                      //
		    "OpenGL context:"         //
		    "\n\tGL_VERSION: %s"      //
		    "\n\tGL_RENDERER: %s"     //
		    "\n\tGL_VENDOR: %s",      //
		    glGetString(GL_VERSION),  //
		    glGetString(GL_RENDERER), //
		    glGetString(GL_VENDOR));  //
	}


	/*
	 * Return to app context.
	 */

	if (need_make_current && !context_make_current(&current_ctx)) {
		U_LOG_E("Failed to make old WGL context current!");
	}


	/*
	 * Checking of context.
	 */

	// Only do error checking here.
	if (wgl_result == 0 || gl_result == 0) {
		U_LOG_E("Failed to load GLAD functions gladLoadWGL: 0x%08x, gladLoadGL: 0x%08x", wgl_result, gl_result);
		FreeLibrary(opengl);
		return NULL;
	}

#define CHECK_REQUIRED_EXTENSION(EXT)                                                                                  \
	do {                                                                                                           \
		if (!GLAD_GL_##EXT) {                                                                                  \
			U_LOG_E("%s - Required OpenGL extension GL_" #EXT " not available", __func__);                 \
			FreeLibrary(opengl);                                                                           \
			return NULL;                                                                                   \
		}                                                                                                      \
	} while (0)

	CHECK_REQUIRED_EXTENSION(EXT_memory_object); // why is this failing? the gpuinfo.org tool says I have it.
	CHECK_REQUIRED_EXTENSION(EXT_memory_object_win32);

#undef CHECK_REQUIRED_EXTENSION


	/*
	 * Checking complete, create client compositor here.
	 */

	struct client_gl_win32_compositor *c = U_TYPED_CALLOC(struct client_gl_win32_compositor);

	// Move the app context to the struct.
	c->app_context = app_ctx;
	// Same for the opengl library handle
	c->opengl = opengl;

	if (!client_gl_compositor_init(&c->base, xcn, client_gl_context_begin, client_gl_context_end,
	                               client_gl_memobj_swapchain_create, NULL)) {
		U_LOG_E("Failed to init parent GL client compositor!");
		FreeLibrary(opengl);
		free(c);
		return NULL;
	}

	c->base.base.base.destroy = client_gl_win32_compositor_destroy;

	return c;
}
