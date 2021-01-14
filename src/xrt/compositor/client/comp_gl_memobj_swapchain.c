// Copyright 2019-2020, Collabora, Ltd.
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

	uint32_t num_images = sc->base.base.base.num_images;
	if (num_images > 0) {
		glDeleteTextures(num_images, &sc->base.base.images[0]);
		U_ZERO_ARRAY(sc->base.base.images);
		glDeleteMemoryObjectsEXT(num_images, &sc->memory[0]);
		U_ZERO_ARRAY(sc->memory);
		sc->base.base.base.num_images = 0;
	}

	// Destroy the native swapchain as well.
	xrt_swapchain_destroy((struct xrt_swapchain **)&sc->base.xscn);

	free(sc);
}
struct xrt_swapchain *
client_gl_memobj_swapchain_create(struct xrt_compositor *xc,
                                  const struct xrt_swapchain_create_info *info,
                                  struct xrt_swapchain_native *xscn,
                                  struct client_gl_swapchain **out_cglsc)
{
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
	struct client_gl_compositor *c = client_gl_compositor(xc);
	(void)c;

	if (xscn == NULL) {
		return NULL;
	}

	GLuint binding_enum = 0;
	GLuint tex_target = 0;
	ogl_texture_target_for_swapchain_info(info, &tex_target, &binding_enum);
	struct xrt_swapchain *native_xsc = &xscn->base;

	struct client_gl_memobj_swapchain *sc = U_TYPED_CALLOC(struct client_gl_memobj_swapchain);
	struct xrt_swapchain_gl *xscgl = &sc->base.base;
	struct xrt_swapchain *client_xsc = &xscgl->base;
	client_xsc->destroy = client_gl_memobj_swapchain_destroy;
	// Fetch the number of images from the native swapchain.
	client_xsc->num_images = native_xsc->num_images;
	sc->base.xscn = xscn;
	sc->base.tex_target = tex_target;

	glGenTextures(native_xsc->num_images, xscgl->images);
	for (uint32_t i = 0; i < native_xsc->num_images; i++) {
		glBindTexture(tex_target, xscgl->images[i]);
	}

	glCreateMemoryObjectsEXT(native_xsc->num_images, &sc->memory[0]);
	for (uint32_t i = 0; i < native_xsc->num_images; i++) {
		GLint dedicated = GL_TRUE;
		glMemoryObjectParameterivEXT(sc->memory[i], GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
		glImportMemoryFdEXT(sc->memory[i], xscn->images[i].size, GL_HANDLE_TYPE_OPAQUE_FD_EXT,
		                    xscn->images[i].handle);

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
	return client_xsc;
#else

	// silence unused function warning
	(void)client_gl_memobj_swapchain_destroy;
	return NULL;
#endif
}
