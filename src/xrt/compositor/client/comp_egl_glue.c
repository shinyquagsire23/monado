// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to EGL client side glue code.
 * @author Drew DeVault <sir@cmpwn.com>
 * @author Simon Ser <contact@emersion.fr>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include "xrt/xrt_config_os.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_gfx_egl.h"
#include "xrt/xrt_handles.h"

#include "util/u_misc.h"
#include "util/u_logging.h"
#include "util/u_debug.h"

#include "ogl/egl_api.h"
#include "ogl/ogl_api.h"

#include "client/comp_gl_client.h"
#include "client/comp_gl_memobj_swapchain.h"
#include "client/comp_gl_eglimage_swapchain.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef XRT_HAVE_EGL
#error "This file shouldn't be compiled without EGL"
#endif

static enum u_logging_level ll;

#define EGL_TRACE(...) U_LOG_IFL_T(ll, __VA_ARGS__)
#define EGL_DEBUG(...) U_LOG_IFL_D(ll, __VA_ARGS__)
#define EGL_INFO(...) U_LOG_IFL_I(ll, __VA_ARGS__)
#define EGL_WARN(...) U_LOG_IFL_W(ll, __VA_ARGS__)
#define EGL_ERROR(...) U_LOG_IFL_E(ll, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(egl_log, "EGL_LOG", U_LOGGING_INFO)


#ifdef XRT_OS_ANDROID
typedef const char *EGLAPIENTRY (*PFNEGLQUERYSTRINGIMPLEMENTATIONANDROIDPROC)(EGLDisplay dpy, EGLint name);
#endif

// Not forward declared by mesa
typedef EGLBoolean
    EGLAPIENTRY (*PFNEGLMAKECURRENTPROC)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);

/*!
 * EGL based compositor.
 */
struct client_egl_compositor
{
	struct client_gl_compositor base;

	EGLDisplay dpy;
};


/*
 *
 * Helper functions.
 *
 */

/*!
 * Down-cast helper.
 * @protected @memberof client_egl_compositor
 */
static inline struct client_egl_compositor *
client_egl_compositor(struct xrt_compositor *xc)
{
	return (struct client_egl_compositor *)xc;
}

XRT_MAYBE_UNUSED static bool
has_extension(const char *extensions, const char *ext)
{
	const char *loc = NULL;
	const char *terminator = NULL;

	if (extensions == NULL) {
		return false;
	}

	while (1) {
		loc = strstr(extensions, ext);
		if (loc == NULL) {
			return false;
		}

		terminator = loc + strlen(ext);
		if ((loc == extensions || *(loc - 1) == ' ') && (*terminator == ' ' || *terminator == '\0')) {
			return true;
		}
		extensions = terminator;
	}
}

static void
ensure_native_fence_is_loaded(EGLDisplay dpy, PFNEGLGETPROCADDRESSPROC get_gl_procaddr)
{
#ifdef XRT_OS_ANDROID
	// clang-format off
	PFNEGLQUERYSTRINGIMPLEMENTATIONANDROIDPROC eglQueryStringImplementationANDROID;
	// clang-format on

	eglQueryStringImplementationANDROID =
	    (PFNEGLQUERYSTRINGIMPLEMENTATIONANDROIDPROC)get_gl_procaddr("eglQueryStringImplementationANDROID");

	// On Android, EGL_ANDROID_native_fence_sync only shows up in this
	// extension list, not the normal one.
	const char *ext = eglQueryStringImplementationANDROID(dpy, EGL_EXTENSIONS);
	if (!has_extension(ext, "EGL_ANDROID_native_fence_sync")) {
		return;
	}

	GLAD_EGL_ANDROID_native_fence_sync = true;
	glad_eglDupNativeFenceFDANDROID =
	    (PFNEGLDUPNATIVEFENCEFDANDROIDPROC)get_gl_procaddr("eglDupNativeFenceFDANDROID");
#endif
}


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
	    .ctx = EGL_NO_CONTEXT,
	    .read = EGL_NO_SURFACE,
	    .draw = EGL_NO_SURFACE,
	};

	// Do we have a valid display?
	if (old.dpy != EGL_NO_DISPLAY) {
		old.ctx = eglGetCurrentContext();
		old.read = eglGetCurrentSurface(EGL_READ);
		old.draw = eglGetCurrentSurface(EGL_DRAW);
	}

	return old;
}

static inline void
old_restore(struct old_helper *old, EGLDisplay current_dpy)
{
	if (old->dpy == EGL_NO_DISPLAY) {
		// There were no display, just unbind the context.
		if (eglMakeCurrent(current_dpy, EGL_NO_CONTEXT, EGL_NO_SURFACE, EGL_NO_SURFACE)) {
			return;
		}
	} else {
		if (eglMakeCurrent(old->dpy, old->draw, old->read, old->ctx)) {
			return;
		}
	}

	EGL_ERROR("Failed to make old EGL context current! (%p, %p, %p, %p)", old->dpy, old->draw, old->read, old->ctx);
}


/*
 *
 * Functions.
 *
 */

static xrt_result_t
insert_fence(struct xrt_compositor *xc, xrt_graphics_sync_handle_t *out_handle)
{
	struct client_egl_compositor *ceglc = client_egl_compositor(xc);

	*out_handle = XRT_GRAPHICS_SYNC_HANDLE_INVALID;
	EGLDisplay dpy = ceglc->dpy;

	if (!GLAD_EGL_ANDROID_native_fence_sync) {
		return XRT_SUCCESS;
	}

#ifdef XRT_GRAPHICS_SYNC_HANDLE_IS_FD

	EGLSyncKHR sync = eglCreateSyncKHR(dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
	if (sync == EGL_NO_SYNC_KHR) {
		EGL_ERROR("Failed to insert fence!");
		return XRT_ERROR_FENCE_CREATE_FAILED;
	}

	glFlush();

	int fence_fd = eglDupNativeFenceFDANDROID(dpy, sync);
	eglDestroySyncKHR(dpy, sync);

	if (fence_fd == EGL_NO_NATIVE_FENCE_FD_ANDROID) {
		EGL_ERROR("Failed to get FD from fence!");
		return XRT_ERROR_NATIVE_HANDLE_FENCE_ERROR;
	}

	*out_handle = fence_fd;

#else
	(void)cglc;
#endif

	return XRT_SUCCESS;
}

static void
client_egl_compositor_destroy(struct xrt_compositor *xc)
{
	struct client_egl_compositor *ceglc = client_egl_compositor(xc);

	free(ceglc);
}

xrt_result_t
xrt_gfx_provider_create_gl_egl(struct xrt_compositor_native *xcn,
                               EGLDisplay display,
                               EGLConfig config,
                               EGLContext context,
                               PFNEGLGETPROCADDRESSPROC get_gl_procaddr,
                               struct xrt_compositor_gl **out_xcgl)
{
	ll = debug_get_log_option_egl_log();

	gladLoadEGL(display, get_gl_procaddr);

	if (config == EGL_NO_CONFIG_KHR && !EGL_KHR_no_config_context) {
		EGL_ERROR("config == EGL_NO_CONFIG_KHR && !EGL_KHR_no_config_context");
		return XRT_ERROR_EGL_CONFIG_MISSING;
	}

	// On Android this extension is 'hidden'.
	ensure_native_fence_is_loaded(display, get_gl_procaddr);

	// Save old display, context and drawables.
	struct old_helper old = old_save();

	if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
		EGL_ERROR("Failed to make EGL context current");
		// No need to restore on failure.
		return XRT_ERROR_OPENGL;
	}

	EGLint egl_client_type;
	if (!eglQueryContext(display, context, EGL_CONTEXT_CLIENT_TYPE, &egl_client_type)) {
		old_restore(&old, display);
		return XRT_ERROR_OPENGL;
	}

	switch (egl_client_type) {
	case EGL_OPENGL_API:
#if defined(XRT_HAVE_OPENGL)
		gladLoadGL(get_gl_procaddr);
		break;
#else
		EGL_ERROR("OpenGL support not including in this runtime build");
		old_restore(&old, display);
		return XRT_ERROR_OPENGL;
#endif

	case EGL_OPENGL_ES_API:
#if defined(XRT_HAVE_OPENGLES)
		gladLoadGLES2(get_gl_procaddr);
		break;
#else
		EGL_ERROR("OpenGL|ES support not including in this runtime build");
		old_restore(&old, display);
		return XRT_ERROR_OPENGL;
#endif
	default: EGL_ERROR("Unsupported EGL client type"); return XRT_ERROR_OPENGL;
	}

	struct client_egl_compositor *ceglc = U_TYPED_CALLOC(struct client_egl_compositor);
	ceglc->dpy = display;

	client_gl_swapchain_create_func sc_create = NULL;

	EGL_DEBUG("Extension availability:");
#define DUMP_EXTENSION_STATUS(EXT) EGL_DEBUG("  - " #EXT ": %s", GLAD_##EXT ? "true" : "false")

	DUMP_EXTENSION_STATUS(GL_EXT_memory_object);
	DUMP_EXTENSION_STATUS(GL_EXT_memory_object_fd);
	DUMP_EXTENSION_STATUS(GL_EXT_memory_object_win32);
	DUMP_EXTENSION_STATUS(GL_OES_EGL_image_external);

	DUMP_EXTENSION_STATUS(EGL_ANDROID_get_native_client_buffer);
	DUMP_EXTENSION_STATUS(EGL_ANDROID_native_fence_sync);
	DUMP_EXTENSION_STATUS(EGL_EXT_image_dma_buf_import_modifiers);
	DUMP_EXTENSION_STATUS(EGL_KHR_fence_sync);
	DUMP_EXTENSION_STATUS(EGL_KHR_image);
	DUMP_EXTENSION_STATUS(EGL_KHR_image_base);
	DUMP_EXTENSION_STATUS(EGL_KHR_reusable_sync);
	DUMP_EXTENSION_STATUS(EGL_KHR_wait_sync);

#undef DUMP_EXTENSION_STATUS

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)

	if (GLAD_GL_EXT_memory_object && GLAD_GL_EXT_memory_object_fd) {
		EGL_DEBUG("Using GL memory object swapchain implementation");
		sc_create = client_gl_memobj_swapchain_create;
	}

	if (sc_create == NULL && GLAD_EGL_EXT_image_dma_buf_import) {
		EGL_DEBUG("Using EGL_Image swapchain implementation");
		sc_create = client_gl_eglimage_swapchain_create;
	}

	if (sc_create == NULL) {
		free(ceglc);
		EGL_ERROR(
		    "Could not find a required extension: need either EGL_EXT_image_dma_buf_import or "
		    "GL_EXT_memory_object_fd");
		old_restore(&old, display);
		return XRT_ERROR_OPENGL;
	}

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)

	EGL_DEBUG("Using EGL_Image swapchain implementation with AHardwareBuffer");
	sc_create = client_gl_eglimage_swapchain_create;

#endif

	if (!client_gl_compositor_init(&ceglc->base, xcn, sc_create, insert_fence)) {
		free(ceglc);
		EGL_ERROR("Failed to initialize compositor");
		old_restore(&old, display);
		return XRT_ERROR_OPENGL;
	}

	ceglc->base.base.base.destroy = client_egl_compositor_destroy;
	old_restore(&old, display);
	*out_xcgl = &ceglc->base.base;

	return XRT_SUCCESS;
}
