// Copyright 2018-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds OpenGL-specific session functions.
 * @author Drew DeVault <sir@cmpwn.com>
 * @author Simon Ser <contact@emersion.fr>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#ifndef XR_USE_PLATFORM_EGL
#error "Must build this file with EGL enabled!"
#endif

#include <stdlib.h>

#include "util/u_misc.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_handle.h"

#include "xrt/xrt_instance.h"

#define EGL_NO_X11              // libglvnd
#define MESA_EGL_NO_X11_HEADERS // mesa
#include <EGL/egl.h>
#include "xrt/xrt_gfx_egl.h"


// Not forward declared by mesa
typedef EGLBoolean(EGLAPIENTRYP PFNEGLQUERYCONTEXTPROC)(EGLDisplay dpy,
                                                        EGLContext ctx,
                                                        EGLint attribute,
                                                        EGLint *value);

XrResult
oxr_session_populate_egl(struct oxr_logger *log,
                         struct oxr_system *sys,
                         XrGraphicsBindingEGLMNDX const *next,
                         struct oxr_session *sess)
{
	EGLint egl_client_type = -1;

	PFNEGLQUERYCONTEXTPROC eglQueryContext = (PFNEGLQUERYCONTEXTPROC)next->getProcAddress("eglQueryContext");
	if (!eglQueryContext) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED, "Call to getProcAddress(eglQueryContext) failed");
	}

	if (!eglQueryContext(next->display, next->context, EGL_CONTEXT_CLIENT_TYPE, &egl_client_type)) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 "Call to eglQueryContext(EGL_CONTEXT_CLIENT_TYPE) failed");
	}

	if (egl_client_type != EGL_OPENGL_API && egl_client_type != EGL_OPENGL_ES_API) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED, "Unsupported EGL client type: '%i'",
		                 egl_client_type);
	}

	struct xrt_compositor_native *xcn = sess->xcn;
	struct xrt_compositor_gl *xcgl = NULL;
	xrt_result_t xret = xrt_gfx_provider_create_gl_egl( //
	    xcn,                                            //
	    next->display,                                  //
	    next->config,                                   //
	    next->context,                                  //
	    next->getProcAddress,                           //
	    &xcgl);                                         //

	if (xret == XRT_ERROR_EGL_CONFIG_MISSING) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "XrGraphicsBindingEGLMNDX::config can not be null when EGL_KHR_no_config_context is "
		                 "not supported by the display.");
	}
	if (xret != XRT_SUCCESS || xcgl == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED, "Failed to create an egl client compositor");
	}

	sess->compositor = &xcgl->base;
	sess->create_swapchain = oxr_swapchain_gl_create;

	return XR_SUCCESS;
}
