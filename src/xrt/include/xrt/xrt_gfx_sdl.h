// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining a XRT graphics provider.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Window SDL_Window;
typedef void *SDL_GLContext;
struct time_state;

/*!
 * Create an OpenGL compositor client using sdl.
 *
 * @ingroup xrt_iface
 * @public @memberof xrt_compositor_native
 */
struct xrt_compositor_gl *
xrt_gfx_provider_create_gl_sdl(struct xrt_compositor_native *xcn, SDL_Window* pWindow, SDL_GLContext sdlCtx);


#ifdef __cplusplus
}
#endif
