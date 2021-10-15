// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to OpenGL Win32 client side code.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include <stdio.h>
#include <stdlib.h>

#include "xrt/xrt_gfx_win32.h"

#include "client/comp_gl_win32_client.h"


struct xrt_compositor_gl *
xrt_gfx_provider_create_gl_win32(struct xrt_compositor_native *xcn, void *hDC, void *hGLRC)
{
	struct client_gl_win32_compositor *xcc = client_gl_win32_compositor_create(xcn, hDC, hGLRC);

	return &xcc->base.base;
}
