// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining a XRT graphics provider.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_compositor.h"

#include "glad/gl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Create an OpenGL compositor client using Win32.
 *
 * @ingroup xrt_iface
 * @public @memberof xrt_compositor_native
 */
struct xrt_compositor_gl *
xrt_gfx_provider_create_gl_win32(struct xrt_compositor_native *xcn, void *hDC, void *hGLRC);


#ifdef __cplusplus
}
#endif
