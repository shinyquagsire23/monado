// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Xlib client side glue to compositor implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include <stdio.h>
#include <stdlib.h>

#include "util/u_misc.h"
#include "util/u_logging.h"

#include "xrt/xrt_gfx_xlib.h"

#include "client/comp_gl_xlib_client.h"
#include "client/comp_gl_memobj_swapchain.h"

#include "ogl/ogl_api.h"


/*!
 * Down-cast helper.
 *
 * @private @memberof client_gl_xlib_compositor
 */
static inline struct client_gl_xlib_compositor *
client_gl_xlib_compositor(struct xrt_compositor *xc)
{
	return (struct client_gl_xlib_compositor *)xc;
}

static void
client_gl_xlib_compositor_destroy(struct xrt_compositor *xc)
{
	struct client_gl_xlib_compositor *c = client_gl_xlib_compositor(xc);

	free(c);
}

typedef void (*void_ptr_func)();

#ifdef __cplusplus
extern "C"
#endif
    void_ptr_func
    glXGetProcAddress(const char *procName);

struct client_gl_xlib_compositor *
client_gl_xlib_compositor_create(struct xrt_compositor_native *xcn,
                                 Display *xDisplay,
                                 uint32_t visualid,
                                 GLXFBConfig glxFBConfig,
                                 GLXDrawable glxDrawable,
                                 GLXContext glxContext)
{
	gladLoadGL(glXGetProcAddress);

#define CHECK_REQUIRED_EXTENSION(EXT)                                                                                  \
	do {                                                                                                           \
		if (!GLAD_##EXT) {                                                                                     \
			U_LOG_E("%s - Required OpenGL extension " #EXT " not available", __func__);                    \
			return NULL;                                                                                   \
		}                                                                                                      \
	} while (0)

	CHECK_REQUIRED_EXTENSION(GL_EXT_memory_object);
#ifdef XRT_OS_LINUX
	CHECK_REQUIRED_EXTENSION(GL_EXT_memory_object_fd);
#endif

#undef CHECK_REQUIRED_EXTENSION

	struct client_gl_xlib_compositor *c = U_TYPED_CALLOC(struct client_gl_xlib_compositor);

	if (!client_gl_compositor_init(&c->base, xcn, client_gl_memobj_swapchain_create, NULL)) {
		free(c);
		return NULL;
	}

	c->base.base.base.destroy = client_gl_xlib_compositor_destroy;

	return c;
}
