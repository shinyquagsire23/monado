// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGL on Win32 client side glue to compositor header.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#pragma once

#include "xrt/xrt_gfx_win32.h"
#include "client/comp_gl_client.h"

#ifdef __cplusplus
extern "C" {
#endif

struct client_gl_context
{
	HDC hDC;
	HGLRC hGLRC;
};

/*!
 * @class client_gl_win32_compositor
 * A client facing win32 OpenGL base compositor.
 *
 * @ingroup comp_client
 * @extends client_gl_compositor
 */
struct client_gl_win32_compositor
{
	//! OpenGL compositor wrapper base.
	struct client_gl_compositor base;

	/*!
	 * Temporary storage for "current" OpenGL context while app_context is
	 * made current using context_begin/context_end. We only need one because
	 * app_context can only be made current in one thread at a time too.
	 */
	struct client_gl_context temp_context;

	//! GL context provided in graphics binding.
	struct client_gl_context app_context;

	//! The OpenGL library
	HMODULE opengl;
};

/*!
 * Create a new client_gl_win32_compositor.
 *
 * @public @memberof client_gl_win32_compositor
 * @see xrt_compositor_native
 */
struct client_gl_win32_compositor *
client_gl_win32_compositor_create(struct xrt_compositor_native *xcn, void *hDC, void *hGLRC);


#ifdef __cplusplus
}
#endif
