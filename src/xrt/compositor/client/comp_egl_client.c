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
#include "client/comp_egl_client.h"
#include "client/comp_gl_memobj_swapchain.h"
#include "client/comp_gl_eglimage_swapchain.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef XRT_HAVE_EGL
#error "This file shouldn't be compiled without EGL"
#endif


/*
 *
 * Logging.
 *
 */

static enum u_logging_level log_level;

#define EGL_TRACE(...) U_LOG_IFL_T(log_level, __VA_ARGS__)
#define EGL_DEBUG(...) U_LOG_IFL_D(log_level, __VA_ARGS__)
#define EGL_INFO(...) U_LOG_IFL_I(log_level, __VA_ARGS__)
#define EGL_WARN(...) U_LOG_IFL_W(log_level, __VA_ARGS__)
#define EGL_ERROR(...) U_LOG_IFL_E(log_level, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(egl_log, "EGL_LOG", U_LOGGING_INFO)


/*
 *
 * Declarations.
 *
 */

#ifdef XRT_OS_ANDROID
typedef const char *EGLAPIENTRY (*PFNEGLQUERYSTRINGIMPLEMENTATIONANDROIDPROC)(EGLDisplay dpy, EGLint name);
#endif

// Not forward declared by mesa
typedef EGLBoolean
    EGLAPIENTRY (*PFNEGLMAKECURRENTPROC)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);

static xrt_result_t
client_egl_insert_fence(struct xrt_compositor *xc, xrt_graphics_sync_handle_t *out_handle);


/*
 *
 * Old helper.
 *
 */

static inline void
save_context(struct client_egl_context *ctx)
{
	ctx->dpy = eglGetCurrentDisplay();
	ctx->ctx = EGL_NO_CONTEXT;
	ctx->read = EGL_NO_SURFACE;
	ctx->draw = EGL_NO_SURFACE;

	if (ctx->dpy != EGL_NO_DISPLAY) {
		ctx->ctx = eglGetCurrentContext();
		ctx->read = eglGetCurrentSurface(EGL_READ);
		ctx->draw = eglGetCurrentSurface(EGL_DRAW);
	}
}

static inline bool
restore_context(struct client_egl_context *ctx)
{
	/* We're using the current display if we're trying to restore a null context */
	EGLDisplay dpy = ctx->dpy == EGL_NO_DISPLAY ? eglGetCurrentDisplay() : ctx->dpy;

	if (dpy == EGL_NO_DISPLAY) {
		/* If the current display is also null then the call is a no-op */
		return true;
	}

	return eglMakeCurrent(dpy, ctx->draw, ctx->read, ctx->ctx);
}


/*
 *
 * Helper functions.
 *
 */

static const char *
egl_error_str(EGLint ret)
{
	switch (ret) {
	case EGL_SUCCESS: return "EGL_SUCCESS";
	case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
	case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
	case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
	case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
	case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
	case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
	case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
	case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
	case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
	case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
	case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
	case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
	case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
	case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
	default: return "EGL_<UNKNOWN>";
	}
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


/*
 *
 * Creation helper functions.
 *
 */

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

static xrt_result_t
load_gl_functions(EGLint egl_client_type, PFNEGLGETPROCADDRESSPROC get_gl_procaddr)
{
	switch (egl_client_type) {
	case EGL_OPENGL_API:
#if defined(XRT_HAVE_OPENGL)
		EGL_DEBUG("Loading GL functions");
		gladLoadGL(get_gl_procaddr);
		break;
#else
		EGL_ERROR("OpenGL support not including in this runtime build");
		return XRT_ERROR_OPENGL;
#endif

	case EGL_OPENGL_ES_API:
#if defined(XRT_HAVE_OPENGLES)
		EGL_DEBUG("Loading GLES2 functions");
		gladLoadGLES2(get_gl_procaddr);
		break;
#else
		EGL_ERROR("OpenGL|ES support not including in this runtime build");
		return XRT_ERROR_OPENGL;
#endif
	default: EGL_ERROR("Unsupported EGL client type: 0x%x", egl_client_type); return XRT_ERROR_OPENGL;
	}

	if (glGetString == NULL) {
		EGL_ERROR("glGetString not loaded!");
		return XRT_ERROR_OPENGL;
	}

	return XRT_SUCCESS;
}

static xrt_result_t
check_context_and_debug_print(EGLint egl_client_type)
{
	EGL_DEBUG(                    //
	    "OpenGL context:"         //
	    "\n\tGL_VERSION: %s"      //
	    "\n\tGL_RENDERER: %s"     //
	    "\n\tGL_VENDOR: %s",      //
	    glGetString(GL_VERSION),  //
	    glGetString(GL_RENDERER), //
	    glGetString(GL_VENDOR));  //


	/*
	 * If a renderer is old enough to not support OpenGL(ES) 3 or above
	 * it won't support Monado at all, it's not a hard requirement and
	 * lets us detect weird errors early on some platforms.
	 */
	if (!GLAD_GL_VERSION_3_0 && !GLAD_GL_ES_VERSION_3_0) {
		switch (egl_client_type) {
		default: EGL_ERROR("Unknown OpenGL version!"); break;
		case EGL_OPENGL_API: EGL_ERROR("Must have OpenGL 3.0 or above!"); break;
		case EGL_OPENGL_ES_API: EGL_ERROR("Must have OpenGL ES 3.0 or above!"); break;
		}

		return XRT_ERROR_OPENGL;
	}


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


	return XRT_SUCCESS;
}

static xrt_result_t
get_client_gl_functions(client_gl_swapchain_create_func_t *out_sc_create_func,
                        client_gl_insert_fence_func_t *out_insert_fence)
{
	client_gl_swapchain_create_func_t sc_create_func = NULL;
	client_gl_insert_fence_func_t insert_fence_func = NULL;


#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)

	if (GLAD_GL_EXT_memory_object && GLAD_GL_EXT_memory_object_fd) {
		EGL_DEBUG("Using GL memory object swapchain implementation");
		sc_create_func = client_gl_memobj_swapchain_create;
	}

	if (sc_create_func == NULL && GLAD_EGL_EXT_image_dma_buf_import) {
		EGL_DEBUG("Using EGL_Image swapchain implementation");
		sc_create_func = client_gl_eglimage_swapchain_create;
	}

	if (sc_create_func == NULL) {
		EGL_ERROR(
		    "Could not find a required extension: need either EGL_EXT_image_dma_buf_import or "
		    "GL_EXT_memory_object_fd");
		return XRT_ERROR_OPENGL;
	}

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)

	EGL_DEBUG("Using EGL_Image swapchain implementation with AHardwareBuffer");
	sc_create_func = client_gl_eglimage_swapchain_create;

#endif

	/*
	 * For now, only use the insert_fence callback only if
	 * EGL_ANDROID_native_fence_sync is available, revisit this when a more
	 * generic synchronization mechanism is implemented.
	 */
	if (GLAD_EGL_ANDROID_native_fence_sync) {
		insert_fence_func = client_egl_insert_fence;
	}

	*out_sc_create_func = sc_create_func;
	*out_insert_fence = insert_fence_func;

	return XRT_SUCCESS;
}


/*
 *
 * GL callback functions.
 *
 */

static xrt_result_t
client_egl_insert_fence(struct xrt_compositor *xc, xrt_graphics_sync_handle_t *out_handle)
{
	struct client_egl_compositor *ceglc = client_egl_compositor(xc);

	*out_handle = XRT_GRAPHICS_SYNC_HANDLE_INVALID;
	EGLDisplay dpy = ceglc->current.dpy;

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

static xrt_result_t
client_egl_context_begin(struct xrt_compositor *xc)
{
	struct client_egl_compositor *eglc = client_egl_compositor(xc);

	save_context(&eglc->previous);
	struct client_egl_context *cur = &eglc->current;

	if (!eglMakeCurrent(cur->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, cur->ctx)) {
		return XRT_ERROR_OPENGL;
	}
	return XRT_SUCCESS;
}

static void
client_egl_context_end(struct xrt_compositor *xc)
{
	struct client_egl_compositor *eglc = client_egl_compositor(xc);

	restore_context(&eglc->previous);
}

static void
client_egl_compositor_destroy(struct xrt_compositor *xc)
{
	struct client_egl_compositor *ceglc = client_egl_compositor(xc);

	client_gl_compositor_close(&ceglc->base);

	free(ceglc);
}


/*
 *
 * 'Exported' functions.
 *
 */

xrt_result_t
xrt_gfx_provider_create_gl_egl(struct xrt_compositor_native *xcn,
                               EGLDisplay display,
                               EGLConfig config,
                               EGLContext context,
                               PFNEGLGETPROCADDRESSPROC get_gl_procaddr,
                               struct xrt_compositor_gl **out_xcgl)
{
	log_level = debug_get_log_option_egl_log();
	xrt_result_t xret;


	/*
	 * Init EGL functions
	 */

	gladLoadEGL(display, get_gl_procaddr);

	if (config == EGL_NO_CONFIG_KHR && !EGL_KHR_no_config_context) {
		EGL_ERROR("config == EGL_NO_CONFIG_KHR && !EGL_KHR_no_config_context");
		return XRT_ERROR_EGL_CONFIG_MISSING;
	}

	// On Android this extension is 'hidden'.
	ensure_native_fence_is_loaded(display, get_gl_procaddr);


	/*
	 * Get client type.
	 */

	EGLint egl_client_type;
	if (!eglQueryContext(display, context, EGL_CONTEXT_CLIENT_TYPE, &egl_client_type)) {
		EGL_ERROR("Could not query EGL client API type from context: %p", (void *)context);
		return XRT_ERROR_OPENGL;
	}


	/*
	 * Make current.
	 */

	// Save old EGL display, context and drawables.
	struct client_egl_context old = {0};
	save_context(&old);

	if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
		EGL_ERROR(
		    "eglMakeCurrent: %s"
		    "\n\tFailed to make EGL context current"
		    "\n\told - dpy: %p, ctx: %p, read: %p, draw: %p"
		    "\n\tnew - dpy: %p, ctx: %p, read: %p, draw: %p",
		    egl_error_str(eglGetError()),                                         //
		    (void *)old.dpy, (void *)old.ctx, (void *)old.read, (void *)old.draw, //
		    (void *)display, (void *)context, NULL, NULL);                        //
		// No need to restore on failure.
		return XRT_ERROR_OPENGL;
	}


	/*
	 * Use helpers to do all setup.
	 */

	// Load GL functions, only EGL functions where loaded above.
	xret = load_gl_functions(egl_client_type, get_gl_procaddr);
	if (xret != XRT_SUCCESS) {
		restore_context(&old);
		return xret;
	}

	// Some consistency/extension availability checking.
	xret = check_context_and_debug_print(egl_client_type);
	if (xret != XRT_SUCCESS) {
		restore_context(&old);
		return xret;
	}

	// Get functions.
	client_gl_swapchain_create_func_t sc_create_func = NULL;
	client_gl_insert_fence_func_t insert_fence_func = NULL;

	xret = get_client_gl_functions(&sc_create_func, &insert_fence_func);
	if (xret != XRT_SUCCESS) {
		restore_context(&old);
		return xret;
	}


	/*
	 * Now do the allocation and init.
	 */

	struct client_egl_compositor *ceglc = U_TYPED_CALLOC(struct client_egl_compositor);
	ceglc->current.dpy = display;
	ceglc->current.ctx = context;

	bool bret = client_gl_compositor_init( //
	    &ceglc->base,                      // c
	    xcn,                               // xcn
	    client_egl_context_begin,          // context_begin
	    client_egl_context_end,            // context_end
	    sc_create_func,                    // create_swapchain
	    insert_fence_func);                // insert_fence
	if (!bret) {
		free(ceglc);
		EGL_ERROR("Failed to initialize compositor");
		restore_context(&old);
		return XRT_ERROR_OPENGL;
	}

	ceglc->base.base.base.destroy = client_egl_compositor_destroy;
	restore_context(&old);
	*out_xcgl = &ceglc->base.base;

	return XRT_SUCCESS;
}
