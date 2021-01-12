// Copyright 2018-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds OpenGLES-specific session functions.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Drew DeVault <sir@cmpwn.com>
 * @author Simon Ser <contact@emersion.fr>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#include <stdlib.h>

#include "util/u_misc.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_handle.h"

#include "xrt/xrt_instance.h"

#ifdef XR_USE_GRAPHICS_API_OPENGL_ES

#include "ogl/ogl_api.h"
#include "ogl/egl_api.h"

#include "xrt/xrt_gfx_egl.h"

#include <dlfcn.h>

XrResult
oxr_session_populate_gles_android(struct oxr_logger *log,
                                  struct oxr_system *sys,
                                  XrGraphicsBindingOpenGLESAndroidKHR const *next,
                                  struct oxr_session *sess)
{
	void *so = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
	if (so == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED, "Could not open libEGL.so");
	}

	PFNEGLGETPROCADDRESSPROC get_proc_addr = (PFNEGLGETPROCADDRESSPROC)dlsym(so, "eglGetProcAddress");
	if (get_proc_addr == NULL) {
		dlclose(so);
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED, "Could not get eglGetProcAddress");
	}

	EGLint egl_client_type;

	PFNEGLQUERYCONTEXTPROC eglQueryContext = (PFNEGLQUERYCONTEXTPROC)get_proc_addr("eglQueryContext");
	if (!eglQueryContext) {
		dlclose(so);
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED, "Call to getProcAddress(eglQueryContext) failed");
	}

	if (!eglQueryContext(next->display, next->context, EGL_CONTEXT_CLIENT_TYPE, &egl_client_type)) {
		dlclose(so);
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 "Call to eglQueryContext(EGL_CONTEXT_CLIENT_TYPE) failed");
	}

	if (egl_client_type != EGL_OPENGL_API && egl_client_type != EGL_OPENGL_ES_API) {
		dlclose(so);
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED, "Unsupported EGL client type");
	}


	struct xrt_compositor_native *xcn = sess->xcn;
	struct xrt_compositor_gl *xcgl = xrt_gfx_provider_create_gl_egl( //
	    xcn,                                                         //
	    next->display,                                               //
	    next->config,                                                //
	    next->context,                                               //
	    get_proc_addr);                                              //

	if (xcgl == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED, "Failed to create an egl client compositor");
	}

	sess->compositor = &xcgl->base;
	sess->create_swapchain = oxr_swapchain_gl_create;

	return XR_SUCCESS;
}

#endif
