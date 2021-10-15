// Copyright 2018-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds OpenGL-specific session functions for Windows
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#ifndef XR_USE_PLATFORM_WIN32
#error "Must build this file with Win32 enabled!"
#endif

#ifndef XR_USE_GRAPHICS_API_OPENGL
#error "Must build this file with OpenGL enabled!"
#endif

#include <stdlib.h>

#include "util/u_misc.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_handle.h"

#include "xrt/xrt_instance.h"
#include "xrt/xrt_gfx_win32.h"


XrResult
oxr_session_populate_gl_win32(struct oxr_logger *log,
                              struct oxr_system *sys,
                              XrGraphicsBindingOpenGLWin32KHR const *next,
                              struct oxr_session *sess)
{

	struct xrt_compositor_native *xcn = sess->xcn;
	struct xrt_compositor_gl *xcgl = xrt_gfx_provider_create_gl_win32(xcn, next->hDC, next->hGLRC);

	if (xcgl == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED, "Failed to create a Win32 client compositor");
	}

	sess->compositor = &xcgl->base;
	sess->create_swapchain = oxr_swapchain_gl_create;

	return XR_SUCCESS;
}
