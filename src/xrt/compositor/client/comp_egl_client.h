// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to EGL client side glue code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#pragma once

#include "xrt/xrt_compositor.h"

#include "ogl/egl_api.h"

#include "client/comp_gl_client.h"


#ifdef __cplusplus
extern "C" {
#endif

struct client_egl_context
{
	EGLDisplay dpy;
	EGLContext ctx;
	EGLSurface read, draw;
};

/*!
 * EGL based compositor, carries the extra needed EGL information needed by the
 * client side code and can handle both GL Desktop or GLES contexts.
 *
 * @ingroup comp_client
 */
struct client_egl_compositor
{
	struct client_gl_compositor base;
	struct client_egl_context current, previous;
};

/*!
 * Down-cast helper.
 * @protected @memberof client_egl_compositor
 */
static inline struct client_egl_compositor *
client_egl_compositor(struct xrt_compositor *xc)
{
	return (struct client_egl_compositor *)xc;
}


#ifdef __cplusplus
}
#endif
