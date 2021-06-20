// Copyright 2018-2020, Collabora, Ltd.
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

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_handle.h"

#include "xrt/xrt_instance.h"

#ifdef XR_USE_PLATFORM_XLIB
#include "xrt/xrt_gfx_xlib.h"
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL
#ifdef XR_USE_PLATFORM_XLIB


XrResult
oxr_session_populate_gl_xlib(struct oxr_logger *log,
                             struct oxr_system *sys,
                             XrGraphicsBindingOpenGLXlibKHR const *next,
                             struct oxr_session *sess)
{
	struct xrt_compositor_native *xcn = sess->xcn;
	struct xrt_compositor_gl *xcgl = xrt_gfx_provider_create_gl_xlib(
	    xcn, next->xDisplay, next->visualid, next->glxFBConfig, next->glxDrawable, next->glxContext);

	if (xcgl == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED, "Failed to create an xlib client compositor");
	}

	sess->compositor = &xcgl->base;
	sess->create_swapchain = oxr_swapchain_gl_create;

	return XR_SUCCESS;
}

#endif // XR_USE_PLATFORM_XLIB

//! @todo add the other OpenGL graphics binding structs here

#endif // XR_USE_GRAPHICS_API_OPENGL
