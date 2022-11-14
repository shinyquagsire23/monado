// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGL on Xlib client side glue to compositor header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#pragma once

#include "xrt/xrt_gfx_xlib.h"
#include "client/comp_gl_client.h"

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

struct client_sdl_gl_context
{
    SDL_Window *window;
    SDL_GLContext ctx;
};

/*!
 * @class client_gl_sdl_compositor
 * A client facing xlib OpenGL base compositor.
 *
 * @ingroup comp_client
 * @extends client_gl_compositor
 */
struct client_gl_sdl_compositor
{
    //! OpenGL compositor wrapper base.
    struct client_gl_compositor base;

    /*!
     * Temporary storage for "current" OpenGL context while app_context is
     * made current using context_begin/context_end. We only need one because
     * app_context can only be made current in one thread at a time too.
     */
    struct client_sdl_gl_context temp_context;

    //! GL context provided in graphics binding.
    struct client_sdl_gl_context app_context;
};

/*!
 * Create a new client_gl_sdl_compositor.
 *
 * @public @memberof client_gl_sdl_compositor
 * @see xrt_compositor_native
 */
struct client_gl_sdl_compositor *
client_gl_sdl_compositor_create(struct xrt_compositor_native *xcn, SDL_Window* pWindow, SDL_GLContext sdlCtx);


#ifdef __cplusplus
}
#endif
