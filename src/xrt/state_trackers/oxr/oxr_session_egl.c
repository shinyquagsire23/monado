// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds OpenGL-specific session functions.
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

#ifdef XR_USE_PLATFORM_EGL
#define EGL_NO_X11              // libglvnd
#define MESA_EGL_NO_X11_HEADERS // mesa
#include <EGL/egl.h>
#include "xrt/xrt_gfx_fd.h"
#include "xrt/xrt_gfx_egl.h"

// Not forward declared by mesa
typedef EGLBoolean(EGLAPIENTRYP PFNEGLQUERYCONTEXTPROC)(EGLDisplay dpy,
                                                        EGLContext ctx,
                                                        EGLint attribute,
                                                        EGLint *value);
#endif

#ifdef XR_USE_PLATFORM_EGL

XrResult
oxr_session_populate_egl(struct oxr_logger *log,
                         struct oxr_system *sys,
                         XrGraphicsBindingEGLMND const *next,
                         struct oxr_session *sess)
{
	EGLint egl_client_type;

	PFNEGLQUERYCONTEXTPROC eglQueryContext =
	    (PFNEGLQUERYCONTEXTPROC)next->getProcAddress("eglQueryContext");
	if (!eglQueryContext) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 "getProcAddress(eglQueryContext) failed");
	}

	if (!eglQueryContext(next->display, next->context,
	                     EGL_CONTEXT_CLIENT_TYPE, &egl_client_type)) {
		return oxr_error(
		    log, XR_ERROR_INITIALIZATION_FAILED,
		    "eglQueryContext(EGL_CONTEXT_CLIENT_TYPE) failed");
	}

	if (egl_client_type != EGL_OPENGL_API &&
	    egl_client_type != EGL_OPENGL_ES_API) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 "unsupported EGL client type");
	}

	struct xrt_compositor_fd *xcfd =
	    xrt_gfx_provider_create_fd(sys->head, true);
	if (xcfd == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 " failed create a fd compositor");
	}

	struct xrt_compositor_gl *xcgl =
	    xrt_gfx_provider_create_gl_egl(xcfd, next->display, next->config,
	                                   next->context, next->getProcAddress);

	if (xcgl == NULL) {
		xcfd->base.destroy(&xcfd->base);
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 " failed create a egl client compositor");
	}

	sess->compositor = &xcgl->base;
	sess->create_swapchain = oxr_swapchain_gl_create;

	return XR_SUCCESS;
}

#endif
