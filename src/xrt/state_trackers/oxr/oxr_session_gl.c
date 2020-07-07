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
	struct xrt_compositor_fd *xcfd = NULL;
	struct xrt_device *xdev = GET_XDEV_BY_ROLE(sess->sys, head);

	int ret = xrt_instance_create_fd_compositor(sys->inst->xinst, xdev,
	                                            true, &xcfd);
	if (ret < 0 || xcfd == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 "Failed to create an fd compositor '%i'", ret);
	}


	struct xrt_compositor_gl *xcgl = xrt_gfx_provider_create_gl_xlib(
	    xcfd, next->xDisplay, next->visualid, next->glxFBConfig,
	    next->glxDrawable, next->glxContext);

	if (xcgl == NULL) {
		xcfd->base.destroy(&xcfd->base);
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 "Failed to create an xlib client compositor");
	}

	sess->compositor = &xcgl->base;
	sess->create_swapchain = oxr_swapchain_gl_create;

	return XR_SUCCESS;
}

#endif // XR_USE_PLATFORM_XLIB

//! @todo add the other OpenGL graphics binding structs here

#endif // XR_USE_GRAPHICS_API_OPENGL
