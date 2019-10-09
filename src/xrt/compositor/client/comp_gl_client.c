// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGL client side glue to compositor implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "util/u_misc.h"

#include "ogl/ogl_api.h"
#include "client/comp_gl_client.h"


/*
 *
 * Swapchain functions.
 *
 */

static void
client_gl_swapchain_destroy(struct xrt_swapchain *xsc)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	uint32_t num_images = sc->base.base.num_images;
	if (num_images > 0) {
		glDeleteTextures(num_images, &sc->base.images[0]);
		U_ZERO_ARRAY(sc->base.images);
		glDeleteMemoryObjectsEXT(num_images, &sc->base.memory[0]);
		U_ZERO_ARRAY(sc->base.images);
		sc->base.base.num_images = 0;
	}

	// Destroy the fd swapchain as well.
	sc->xscfd->base.destroy(&sc->xscfd->base);

	free(sc);
}

static bool
client_gl_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *index)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return sc->xscfd->base.acquire_image(&sc->xscfd->base, index);
}

static bool
client_gl_swapchain_wait_image(struct xrt_swapchain *xsc,
                               uint64_t timeout,
                               uint32_t index)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return sc->xscfd->base.wait_image(&sc->xscfd->base, timeout, index);
}

static bool
client_gl_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return sc->xscfd->base.release_image(&sc->xscfd->base, index);
}


/*
 *
 * Compositor functions.
 *
 */

static void
client_gl_compositor_begin_session(struct xrt_compositor *xc,
                                   enum xrt_view_type type)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into fd compositor.
	c->xcfd->base.begin_session(&c->xcfd->base, type);
}

static void
client_gl_compositor_end_session(struct xrt_compositor *xc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into fd compositor.
	c->xcfd->base.end_session(&c->xcfd->base);
}

static void
client_gl_compositor_wait_frame(struct xrt_compositor *xc,
                                int64_t *predicted_display_time,
                                int64_t *predicted_display_period)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into fd compositor.
	c->xcfd->base.wait_frame(&c->xcfd->base, predicted_display_time,
	                         predicted_display_period);
}

static void
client_gl_compositor_begin_frame(struct xrt_compositor *xc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into fd compositor.
	c->xcfd->base.begin_frame(&c->xcfd->base);
}

static void
client_gl_compositor_discard_frame(struct xrt_compositor *xc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into fd compositor.
	c->xcfd->base.discard_frame(&c->xcfd->base);
}

static void
client_gl_compositor_end_frame(struct xrt_compositor *xc,
                               enum xrt_blend_mode blend_mode,
                               struct xrt_swapchain **xscs,
                               const uint32_t *image_index,
                               uint32_t *layers,
                               uint32_t num_swapchains)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *internal[8];

	if (num_swapchains > 8) {
		fprintf(stderr, "ERROR! %s\n", __func__);
		return;
	}

	for (uint32_t i = 0; i < num_swapchains; i++) {
		struct client_gl_swapchain *sc = client_gl_swapchain(xscs[i]);
		internal[i] = &sc->xscfd->base;
	}

	// Pipe down call into fd compositor.
	c->xcfd->base.end_frame(&c->xcfd->base, blend_mode, internal,
	                        image_index, layers, num_swapchains);
}

static int64_t
gl_format_to_vk(int64_t format)
{
	switch (format) {
	case GL_RGBA8: return 37 /*VK_FORMAT_R8G8B8A8_UNORM*/;
	case GL_SRGB8_ALPHA8: return 43 /*VK_FORMAT_R8G8B8A8_SRGB*/;
	default: return 0;
	}
}

static struct xrt_swapchain *
client_gl_swapchain_create(struct xrt_compositor *xc,
                           enum xrt_swapchain_create_flags create,
                           enum xrt_swapchain_usage_bits bits,
                           int64_t format,
                           uint32_t sample_count,
                           uint32_t width,
                           uint32_t height,
                           uint32_t face_count,
                           uint32_t array_size,
                           uint32_t mip_count)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	uint32_t num_images = 3;

	int64_t vk_format = gl_format_to_vk(format);
	if (vk_format == 0) {
		fprintf(stderr, "%s - Invalid format!\n", __func__);
		return NULL;
	}

	struct xrt_swapchain *xsc = c->xcfd->base.create_swapchain(
	    &c->xcfd->base, create, bits, vk_format, sample_count, width,
	    height, face_count, array_size, mip_count);

	if (xsc == NULL) {
		return NULL;
	}

	struct client_gl_swapchain *sc =
	    U_TYPED_CALLOC(struct client_gl_swapchain);
	sc->base.base.destroy = client_gl_swapchain_destroy;
	sc->base.base.acquire_image = client_gl_swapchain_acquire_image;
	sc->base.base.wait_image = client_gl_swapchain_wait_image;
	sc->base.base.release_image = client_gl_swapchain_release_image;
	sc->base.base.num_images = num_images;
	sc->xscfd = xrt_swapchain_fd(xsc);

	glCreateTextures(array_size == 1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_ARRAY,
	                 num_images, &sc->base.images[0]);
	glCreateMemoryObjectsEXT(num_images, &sc->base.memory[0]);
	for (uint32_t i = 0; i < num_images; i++) {
		GLint dedicated = GL_TRUE;
		glMemoryObjectParameterivEXT(sc->base.memory[i],
		                             GL_DEDICATED_MEMORY_OBJECT_EXT,
		                             &dedicated);
		glImportMemoryFdEXT(
		    sc->base.memory[i], sc->xscfd->images[i].size,
		    GL_HANDLE_TYPE_OPAQUE_FD_EXT, sc->xscfd->images[i].fd);
		if (array_size == 1) {
			glTextureStorageMem2DEXT(sc->base.images[i], mip_count,
			                         (GLuint)format, width, height,
			                         sc->base.memory[i], 0);
		} else {
			glTextureStorageMem3DEXT(
			    sc->base.images[i], mip_count, (GLuint)format,
			    width, height, array_size, sc->base.memory[i], 0);
		}
	}

	return &sc->base.base;
}

bool
client_gl_compositor_init(struct client_gl_compositor *c,
                          struct xrt_compositor_fd *xcfd,
                          client_gl_get_procaddr get_gl_procaddr)
{
	c->base.base.create_swapchain = client_gl_swapchain_create;
	c->base.base.begin_session = client_gl_compositor_begin_session;
	c->base.base.end_session = client_gl_compositor_end_session;
	c->base.base.wait_frame = client_gl_compositor_wait_frame;
	c->base.base.begin_frame = client_gl_compositor_begin_frame;
	c->base.base.discard_frame = client_gl_compositor_discard_frame;
	c->base.base.end_frame = client_gl_compositor_end_frame;
	c->base.base.formats[0] = GL_SRGB8_ALPHA8;
	c->base.base.formats[1] = GL_RGBA8;
	c->base.base.num_formats = 2;
	c->xcfd = xcfd;

	gladLoadGL(get_gl_procaddr);

	if (!GLAD_GL_EXT_memory_object_fd) {
		// @todo log this to a proper logger.
		fprintf(stderr,
		        "%s - Needed extension"
		        " GL_EXT_memory_object_fd not supported\n",
		        __func__);
		return false;
	}

	return true;
}
