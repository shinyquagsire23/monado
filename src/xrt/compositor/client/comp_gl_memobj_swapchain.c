// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGL client side glue to compositor implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include <xrt/xrt_config_have.h>
#include <xrt/xrt_handles.h>

#include "util/u_misc.h"

#include "ogl/ogl_api.h"
#include "ogl/ogl_helpers.h"

#include "client/comp_gl_client.h"
#include "client/comp_gl_memobj_swapchain.h"

#include <inttypes.h>

/*!
 * Down-cast helper.
 * @private @memberof client_gl_memobj_swapchain
 */
static inline struct client_gl_memobj_swapchain *
client_gl_memobj_swapchain(struct xrt_swapchain *xsc)
{
	return (struct client_gl_memobj_swapchain *)xsc;
}


/*
 *
 * Swapchain functions.
 *
 */

static void
client_gl_memobj_swapchain_destroy(struct xrt_swapchain *xsc)
{
	struct client_gl_memobj_swapchain *sc = client_gl_memobj_swapchain(xsc);

	uint32_t image_count = sc->base.base.base.image_count;

	struct client_gl_compositor *c = sc->base.gl_compositor;
	enum xrt_result xret = client_gl_compositor_context_begin(&c->base.base, CLIENT_GL_CONTEXT_REASON_OTHER);

	if (image_count > 0) {
		if (xret == XRT_SUCCESS) {
			glDeleteTextures(image_count, &sc->base.base.images[0]);
			glDeleteMemoryObjectsEXT(image_count, &sc->memory[0]);
		}

		U_ZERO_ARRAY(sc->base.base.images);
		U_ZERO_ARRAY(sc->memory);
		sc->base.base.base.image_count = 0;
	}

	if (xret == XRT_SUCCESS) {
		client_gl_compositor_context_end(&c->base.base, CLIENT_GL_CONTEXT_REASON_OTHER);
	}

	// Drop our reference, does NULL checking.
	xrt_swapchain_reference((struct xrt_swapchain **)&sc->base.xscn, NULL);

	free(sc);
}

static bool
client_gl_memobj_swapchain_import(GLuint memory, size_t size, xrt_graphics_buffer_handle_t handle)
{
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
	glImportMemoryFdEXT(memory, size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, handle);
	return true;
#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)
	glImportMemoryWin32HandleEXT(memory, size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handle);
	return true;
#else
	(void)memory;
	(void)size;
	(void)handle;
	return false;
#endif
}

struct xrt_swapchain *
client_gl_memobj_swapchain_create(struct xrt_compositor *xc,
                                  const struct xrt_swapchain_create_info *info,
                                  struct xrt_swapchain_native *xscn,
                                  struct client_gl_swapchain **out_cglsc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	(void)c;

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD) || defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)
	if (xscn == NULL) {
		return NULL;
	}

	GLuint binding_enum = 0;
	GLuint tex_target = 0;
	ogl_texture_target_for_swapchain_info(info, &tex_target, &binding_enum);
	struct xrt_swapchain *native_xsc = &xscn->base;

	struct client_gl_memobj_swapchain *sc = U_TYPED_CALLOC(struct client_gl_memobj_swapchain);
	sc->base.base.base.destroy = client_gl_memobj_swapchain_destroy;
	sc->base.base.base.reference.count = 1;
	sc->base.base.base.image_count =
	    native_xsc->image_count; // Fetch the number of images from the native swapchain.
	sc->base.xscn = xscn;
	sc->base.tex_target = tex_target;

	sc->base.gl_compositor = c;

	struct xrt_swapchain_gl *xscgl = &sc->base.base;
	glGenTextures(native_xsc->image_count, xscgl->images);

	glCreateMemoryObjectsEXT(native_xsc->image_count, &sc->memory[0]);
	for (uint32_t i = 0; i < native_xsc->image_count; i++) {
		glBindTexture(tex_target, xscgl->images[i]);

		GLint dedicated = xscn->images[i].use_dedicated_allocation ? GL_TRUE : GL_FALSE;
		glMemoryObjectParameterivEXT(sc->memory[i], GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);

		if (!client_gl_memobj_swapchain_import(sc->memory[i], xscn->images[i].size, xscn->images[i].handle)) {
			continue;
		}

		// We have consumed this now, make sure it's not freed again.
		xscn->images[i].handle = XRT_GRAPHICS_BUFFER_HANDLE_INVALID;

		if (info->array_size == 1) {
			glTextureStorageMem2DEXT(xscgl->images[i], info->mip_count, (GLuint)info->format, info->width,
			                         info->height, sc->memory[i], 0);
		} else {
			glTextureStorageMem3DEXT(xscgl->images[i], info->mip_count, (GLuint)info->format, info->width,
			                         info->height, info->array_size, sc->memory[i], 0);
		}
	}

	*out_cglsc = &sc->base;
	return &sc->base.base.base;
#else

	// silence unused function warning
	(void)client_gl_memobj_swapchain_destroy;
	return NULL;
#endif
}
