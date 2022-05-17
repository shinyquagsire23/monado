// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGL client side glue to compositor header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#pragma once

#include "xrt/xrt_compositor.h"
#include "os/os_threading.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *
 * Structs
 *
 */

struct client_gl_compositor;

/*!
 * @class client_gl_swapchain
 *
 * Wraps the real compositor swapchain providing a OpenGL based interface.
 *
 * Almost a one to one mapping to a OpenXR swapchain.
 *
 * @ingroup comp_client
 * @implements xrt_swapchain_gl
 */
struct client_gl_swapchain
{
	//! Implements @ref xrt_swapchain_gl
	struct xrt_swapchain_gl base;

	struct xrt_swapchain_native *xscn;

	//! The texture target of images in this swapchain.
	uint32_t tex_target;

	/*!
	 * The compositor this swapchain was created on. Used when swapchain functions need to use the GL context.
	 *
	 * Not an owning pointer.
	 */
	struct client_gl_compositor *gl_compositor;
};

/*!
 * Fetches the OpenGL context that is current on this thread and makes the OpenGL context given in the graphics binding
 * current instead. Only one thread at a time can operate on the sections between @ref client_gl_context_begin_func_t
 * and
 * @ref client_gl_context_end_func_t, therefore client_gl_context_end_func_t MUST be called to avoid blocking the next
 * thread calling @ref client_gl_context_begin_func_t.
 *
 * If the return value is not XRT_SUCCESS, @ref client_gl_context_end_func_t should not be called.
 */
typedef xrt_result_t (*client_gl_context_begin_func_t)(struct xrt_compositor *xc);

/*!
 * Makes the OpenGL context current that was current before @ref client_gl_context_begin_func_t was called.
 */
typedef void (*client_gl_context_end_func_t)(struct xrt_compositor *xc);

/*!
 * The type of a swapchain create constructor.
 *
 * Because our swapchain creation varies depending on available extensions and
 * application choices, the swapchain constructor parameter to
 * client_gl_compositor is parameterized.
 *
 * Note that the "common" swapchain creation function does some setup before
 * invoking this, and some cleanup after.
 *
 * - Must populate `destroy`
 * - Does not need to save/restore texture binding
 */
typedef struct xrt_swapchain *(*client_gl_swapchain_create_func_t)(struct xrt_compositor *xc,
                                                                   const struct xrt_swapchain_create_info *info,
                                                                   struct xrt_swapchain_native *xscn,
                                                                   struct client_gl_swapchain **out_sc);

/*!
 * The type of a fence insertion function.
 *
 * This function is called in xrt_compositor::layer_commit.
 *
 * The returned graphics sync handle is given to xrt_compositor::layer_commit.
 */
typedef xrt_result_t (*client_gl_insert_fence_func_t)(struct xrt_compositor *xc,
                                                      xrt_graphics_sync_handle_t *out_handle);

/*!
 * @class client_gl_compositor
 *
 * Wraps the real compositor providing a OpenGL based interface.
 *
 * @ingroup comp_client
 * @implements xrt_compositor_gl
 */
struct client_gl_compositor
{
	struct xrt_compositor_gl base;

	struct xrt_compositor_native *xcn;

	/*!
	 * Function pointer for making the OpenGL context current.
	 */
	client_gl_context_begin_func_t context_begin;

	/*!
	 * Function pointer for restoring prior OpenGL context.
	 */
	client_gl_context_end_func_t context_end;

	/*!
	 * Function pointer for creating the client swapchain.
	 */
	client_gl_swapchain_create_func_t create_swapchain;

	/*!
	 * Function pointer for inserting fences on
	 * xrt_compositor::layer_commit.
	 */
	client_gl_insert_fence_func_t insert_fence;

	/*!
	 * @ref client_gl_xlib_compositor::app_context can only be current on one thread; block other threads while we
	 * know it is bound to a thread.
	 */
	struct os_mutex context_mutex;
};


/*
 *
 * Functions and helpers.
 *
 */

/*!
 * Down-cast helper.
 * @protected @memberof client_gl_compositor
 */
static inline struct client_gl_compositor *
client_gl_compositor(struct xrt_compositor *xc)
{
	return (struct client_gl_compositor *)xc;
}

/*!
 * Fill in a client_gl_compositor and do common OpenGL readiness checking.
 *
 * OpenGL can have multiple backing window systems we have to interact with, so
 * there isn't just one unified OpenGL client constructor.
 *
 * Moves ownership of provided xcn to the client_gl_compositor.
 *
 * Be sure to load your GL loader/wrapper (GLAD) before calling into here, it
 * won't be called for you.
 *
 * @public @memberof client_gl_compositor
 * @see xrt_compositor_native
 */
bool
client_gl_compositor_init(struct client_gl_compositor *c,
                          struct xrt_compositor_native *xcn,
                          client_gl_context_begin_func_t context_begin,
                          client_gl_context_end_func_t context_end,
                          client_gl_swapchain_create_func_t create_swapchain,
                          client_gl_insert_fence_func_t insert_fence);

/*!
 * Free all resources from the client_gl_compositor, does not free the
 * @ref client_gl_compositor itself. Nor does it free the
 * @ref xrt_compositor_native given at init as that is not owned by us.
 *
 * @public @memberof client_gl_compositor
 */
void
client_gl_compositor_close(struct client_gl_compositor *c);


#ifdef __cplusplus
}
#endif
