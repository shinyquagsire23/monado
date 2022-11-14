// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  SDL2 client side glue to compositor implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include <stdio.h>
#include <stdlib.h>

#include "util/u_misc.h"
#include "util/u_logging.h"

#include "xrt/xrt_gfx_sdl.h"

#include "client/comp_gl_sdl_client.h"
#include "client/comp_gl_memobj_swapchain.h"

#include "ogl/ogl_api.h"
//#include "ogl/glx_api.h"

#include <SDL.h>

/*
 *
 * OpenGL context helper.
 *
 */

static inline bool
context_matches(const struct client_sdl_gl_context *a, const struct client_sdl_gl_context *b)
{
    return a->ctx == b->ctx && a->window == b->window;
}

static inline void
context_save_current(struct client_sdl_gl_context *current_ctx)
{
    current_ctx->window = SDL_GL_GetCurrentWindow();
    current_ctx->ctx = SDL_GL_GetCurrentContext();
}

static inline bool
context_make_current(const struct client_sdl_gl_context *ctx_to_make_current)
{
    if (SDL_GL_MakeCurrent(ctx_to_make_current->window, ctx_to_make_current->ctx)) {
        return true;
    }
    return true;
}

/*!
 * Down-cast helper.
 *
 * @private @memberof client_gl_sdl_compositor
 */
static inline struct client_gl_sdl_compositor *
client_gl_sdl_compositor(struct xrt_compositor *xc)
{
    return (struct client_gl_sdl_compositor *)xc;
}

static void
client_gl_sdl_compositor_destroy(struct xrt_compositor *xc)
{
    struct client_gl_sdl_compositor *c = client_gl_sdl_compositor(xc);

    client_gl_compositor_close(&c->base);

    free(c);
}

static xrt_result_t
client_gl_context_begin(struct xrt_compositor *xc)
{
    struct client_gl_sdl_compositor *c = client_gl_sdl_compositor(xc);

    struct client_sdl_gl_context *app_ctx = &c->app_context;

    os_mutex_lock(&c->base.context_mutex);

    context_save_current(&c->temp_context);

    bool need_make_current = !context_matches(&c->temp_context, app_ctx);

    U_LOG_T("GL Context begin: need makeCurrent: %d (current %p -> app %p)", need_make_current,
            (void *)c->temp_context.ctx, (void *)app_ctx->ctx);

    if (need_make_current && !context_make_current(app_ctx)) {
        os_mutex_unlock(&c->base.context_mutex);

        U_LOG_E("Failed to make SDL context current");
        // No need to restore on failure.
        return XRT_ERROR_OPENGL;
    }

    return XRT_SUCCESS;
}

static void
client_gl_context_end(struct xrt_compositor *xc)
{
    struct client_gl_sdl_compositor *c = client_gl_sdl_compositor(xc);

    struct client_sdl_gl_context *app_ctx = &c->app_context;

    struct client_sdl_gl_context *current_sdl_context = &c->temp_context;

    bool need_make_current = !context_matches(&c->temp_context, app_ctx);

    U_LOG_T("GL Context end: need makeCurrent: %d (app %p -> current %p)", need_make_current, (void *)app_ctx->ctx,
            (void *)c->temp_context.ctx);

    if (need_make_current && !context_make_current(current_sdl_context)) {
        U_LOG_E("Failed to make old SDL context current! (%p, %p)",
                (void *)current_sdl_context->window, (void *)current_sdl_context->ctx);
        // fall through to os_mutex_unlock even if we didn't succeed in restoring the context
    }

    os_mutex_unlock(&c->base.context_mutex);
}

typedef void (*void_ptr_func)();

struct client_gl_sdl_compositor *
client_gl_sdl_compositor_create(struct xrt_compositor_native *xcn, SDL_Window* pWindow, SDL_GLContext sdlCtx)
{
    // Save old SDL context.
    struct client_sdl_gl_context current_ctx;
    context_save_current(&current_ctx);

    // The context and drawables given from the app.
    struct client_sdl_gl_context app_ctx = {
        .window = pWindow,
        .ctx = sdlCtx,
    };


    bool need_make_current = !context_matches(&current_ctx, &app_ctx);

    U_LOG_T("GL Compositor create: need makeCurrent: %d (current %p -> app %p)", need_make_current,
            (void *)current_ctx.ctx, (void *)app_ctx.ctx);

    if (need_make_current && !context_make_current(&app_ctx)) {
        U_LOG_E("Failed to make SDL context current");
        // No need to restore on failure.
        return NULL;
    }

    gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);


    //U_LOG_T("GL Compositor create: need makeCurrent: %d (app %p -> current %p)", need_make_current,
    //        (void *)app_ctx.ctx, (void *)current_ctx.ctx);

    if (need_make_current && !context_make_current(&current_ctx)) {
        U_LOG_E("Failed to make old SDL context current! (%p, %p)", (void *)current_ctx.window, (void *)current_ctx.ctx);
    }

#define CHECK_REQUIRED_EXTENSION(EXT)                                                                                  \
    do {                                                                                                           \
        if (!GLAD_##EXT) {                                                                                     \
            U_LOG_E("%s - Required OpenGL extension " #EXT " not available", __func__);                    \
            return NULL;                                                                                   \
        }                                                                                                      \
    } while (0)

    //CHECK_REQUIRED_EXTENSION(GL_EXT_memory_object);
#ifdef XRT_OS_LINUX
    CHECK_REQUIRED_EXTENSION(GL_EXT_memory_object_fd);
#endif

#undef CHECK_REQUIRED_EXTENSION

    struct client_gl_sdl_compositor *c = U_TYPED_CALLOC(struct client_gl_sdl_compositor);

    // Move the app context to the struct.
    c->app_context = app_ctx;

    if (!client_gl_compositor_init(&c->base, xcn, client_gl_context_begin, client_gl_context_end,
                                   client_gl_memobj_swapchain_create, NULL)) {
        free(c);
        return NULL;
    }

    c->base.base.base.destroy = client_gl_sdl_compositor_destroy;

    return c;
}
