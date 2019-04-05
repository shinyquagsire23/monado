// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds OpenGL-specific session functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#include <stdlib.h>

#include "util/u_misc.h"

#include "xrt/xrt_gfx_xlib.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_handle.h"


XrResult
oxr_session_create_gl_xlib(struct oxr_logger *log,
                           struct oxr_system *sys,
                           XrGraphicsBindingOpenGLXlibKHR *next,
                           struct oxr_session **out_session)
{
	struct xrt_compositor_gl *xcgl = xrt_gfx_provider_create_gl_xlib(
	    sys->device, sys->inst->timekeeping, next->xDisplay, next->visualid,
	    next->glxFBConfig, next->glxDrawable, next->glxContext);

	if (xcgl == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 " failed create a compositor");
	}

	struct oxr_session *sess = NULL;
	XrResult result =
	    OXR_ALLOCATE_HANDLE(log, sess, OXR_XR_DEBUG_SESSION,
	                        oxr_session_destroy, &sys->inst->handle);
	if (result != XR_SUCCESS) {
		xcgl->base.destroy(&xcgl->base);
		return result;
	}
	sess->sys = sys;
	sess->compositor = &xcgl->base;
	sess->create_swapchain = oxr_swapchain_gl_create;

	*out_session = sess;

	return XR_SUCCESS;
}
