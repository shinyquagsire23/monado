// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Glue code to OpenGL SDL2 client side code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include <stdio.h>
#include <stdlib.h>

#include "xrt/xrt_gfx_sdl.h"

#include "client/comp_gl_sdl_client.h"


struct xrt_compositor_gl *
xrt_gfx_provider_create_gl_sdl(struct xrt_compositor_native *xcn, SDL_Window* pWindow, SDL_GLContext sdlCtx)
{
    struct client_gl_sdl_compositor *xcc =
        client_gl_sdl_compositor_create(xcn, pWindow, sdlCtx);

    return &xcc->base.base;
}
