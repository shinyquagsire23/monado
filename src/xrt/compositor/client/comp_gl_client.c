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

#include "util/u_misc.h"

#include "ogl/ogl_api.h"
#include "client/comp_gl_client.h"

#include <inttypes.h>

/*!
 * Down-cast helper.
 * @private @memberof client_gl_swapchain
 */
static inline struct client_gl_swapchain *
client_gl_swapchain(struct xrt_swapchain *xsc)
{
	return (struct client_gl_swapchain *)xsc;
}

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
	xrt_swapchain_destroy((struct xrt_swapchain **)&sc->xscfd);

	free(sc);
}

static xrt_result_t
client_gl_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *index)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return xrt_swapchain_acquire_image(&sc->xscfd->base, index);
}

static xrt_result_t
client_gl_swapchain_wait_image(struct xrt_swapchain *xsc,
                               uint64_t timeout,
                               uint32_t index)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return xrt_swapchain_wait_image(&sc->xscfd->base, timeout, index);
}

static xrt_result_t
client_gl_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	// Pipe down call into fd swapchain.
	return xrt_swapchain_release_image(&sc->xscfd->base, index);
}


/*
 *
 * Compositor functions.
 *
 */

static xrt_result_t
client_gl_compositor_begin_session(struct xrt_compositor *xc,
                                   enum xrt_view_type type)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into fd compositor.
	return xrt_comp_begin_session(&c->xcfd->base, type);
}

static xrt_result_t
client_gl_compositor_end_session(struct xrt_compositor *xc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into fd compositor.
	return xrt_comp_end_session(&c->xcfd->base);
}

static xrt_result_t
client_gl_compositor_wait_frame(struct xrt_compositor *xc,
                                uint64_t *predicted_display_time,
                                uint64_t *predicted_display_period)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into fd compositor.
	return xrt_comp_wait_frame(&c->xcfd->base, predicted_display_time,
	                           predicted_display_period);
}

static xrt_result_t
client_gl_compositor_begin_frame(struct xrt_compositor *xc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into fd compositor.
	return xrt_comp_begin_frame(&c->xcfd->base);
}

static xrt_result_t
client_gl_compositor_discard_frame(struct xrt_compositor *xc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	// Pipe down call into fd compositor.
	return xrt_comp_discard_frame(&c->xcfd->base);
}

static xrt_result_t
client_gl_compositor_layer_begin(struct xrt_compositor *xc,
                                 enum xrt_blend_mode env_blend_mode)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	return xrt_comp_layer_begin(&c->xcfd->base, env_blend_mode);
}

static xrt_result_t
client_gl_compositor_layer_stereo_projection(struct xrt_compositor *xc,
                                             struct xrt_device *xdev,
                                             struct xrt_swapchain *l_xsc,
                                             struct xrt_swapchain *r_xsc,
                                             struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *l_xscfd, *r_xscfd;

	assert(data->type == XRT_LAYER_STEREO_PROJECTION);

	l_xscfd = &client_gl_swapchain(l_xsc)->xscfd->base;
	r_xscfd = &client_gl_swapchain(r_xsc)->xscfd->base;
	data->flip_y = true;

	return xrt_comp_layer_stereo_projection(&c->xcfd->base, xdev, l_xscfd,
	                                        r_xscfd, data);
}

static xrt_result_t
client_gl_compositor_layer_quad(struct xrt_compositor *xc,
                                struct xrt_device *xdev,
                                struct xrt_swapchain *xsc,
                                struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_QUAD);

	xscfb = &client_gl_swapchain(xsc)->xscfd->base;
	data->flip_y = true;

	return xrt_comp_layer_quad(&c->xcfd->base, xdev, xscfb, data);
}

static xrt_result_t
client_gl_compositor_layer_commit(struct xrt_compositor *xc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	return xrt_comp_layer_commit(&c->xcfd->base);
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

static int64_t
vk_format_to_gl(int64_t format)
{
	switch (format) {
	case 37 /*VK_FORMAT_R8G8B8A8_UNORM*/: return GL_RGBA8;
	case 43 /*VK_FORMAT_R8G8B8A8_SRGB*/: return GL_SRGB8_ALPHA8;
	case 44 /*VK_FORMAT_B8G8R8A8_UNORM*/: return 0;
	case 50 /*VK_FORMAT_B8G8R8A8_SRGB*/: return 0;
	default:
		printf("Cannot convert VK format 0x%016" PRIx64
		       " to GL format!\n",
		       format);
		return 0;
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

	if (array_size > 1) {
		const char *version_str = (const char *)glGetString(GL_VERSION);
		if (strstr(version_str, "OpenGL ES 2.") == version_str) {
			fprintf(stderr,
			        "%s - only one array layer is supported with "
			        "OpenGL ES 2\n",
			        __func__);
			return NULL;
		}
	}

	int64_t vk_format = gl_format_to_vk(format);
	if (vk_format == 0) {
		fprintf(stderr, "%s - Invalid format!\n", __func__);
		return NULL;
	}

	struct xrt_swapchain_fd *xscfd = xrt_comp_fd_create_swapchain(
	    c->xcfd, create, bits, vk_format, sample_count, width, height,
	    face_count, array_size, mip_count);


	if (xscfd == NULL) {
		return NULL;
	}
	struct xrt_swapchain *xsc = &xscfd->base;

	struct client_gl_swapchain *sc =
	    U_TYPED_CALLOC(struct client_gl_swapchain);
	sc->base.base.destroy = client_gl_swapchain_destroy;
	sc->base.base.acquire_image = client_gl_swapchain_acquire_image;
	sc->base.base.wait_image = client_gl_swapchain_wait_image;
	sc->base.base.release_image = client_gl_swapchain_release_image;
	// Fetch the number of images from the fd swapchain.
	sc->base.base.num_images = xsc->num_images;
	sc->xscfd = xscfd;

	GLuint prev_texture = 0;
	glGetIntegerv(array_size == 1 ? GL_TEXTURE_BINDING_2D
	                              : GL_TEXTURE_BINDING_2D_ARRAY,
	              (GLint *)&prev_texture);

	glGenTextures(xsc->num_images, sc->base.images);
	for (uint32_t i = 0; i < xsc->num_images; i++) {
		glBindTexture(array_size == 1 ? GL_TEXTURE_2D
		                              : GL_TEXTURE_2D_ARRAY,
		              sc->base.images[i]);
	}
	glCreateMemoryObjectsEXT(xsc->num_images, &sc->base.memory[0]);
	for (uint32_t i = 0; i < xsc->num_images; i++) {
		GLint dedicated = GL_TRUE;
		glMemoryObjectParameterivEXT(sc->base.memory[i],
		                             GL_DEDICATED_MEMORY_OBJECT_EXT,
		                             &dedicated);
		glImportMemoryFdEXT(sc->base.memory[i], xscfd->images[i].size,
		                    GL_HANDLE_TYPE_OPAQUE_FD_EXT,
		                    xscfd->images[i].fd);

		// We have consumed this fd now, make sure it's not freed again.
		xscfd->images[i].fd = -1;

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

	glBindTexture(array_size == 1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_ARRAY,
	              prev_texture);

	return &sc->base.base;
}

static void
client_gl_compositor_destroy(struct xrt_compositor *xc)
{
	assert(!"Destroy should be implemented by the winsys code that uses the GL code.");
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
	c->base.base.layer_begin = client_gl_compositor_layer_begin;
	c->base.base.layer_stereo_projection =
	    client_gl_compositor_layer_stereo_projection;
	c->base.base.layer_quad = client_gl_compositor_layer_quad;
	c->base.base.layer_commit = client_gl_compositor_layer_commit;
	c->base.base.destroy = client_gl_compositor_destroy;
	c->xcfd = xcfd;

	// Passthrough our formats from the fd compositor to the client.
	size_t count = 0;
	for (uint32_t i = 0; i < xcfd->base.num_formats; i++) {
		int64_t f = vk_format_to_gl(xcfd->base.formats[i]);
		if (f == 0) {
			continue;
		}

		c->base.base.formats[count++] = f;
	}
	c->base.base.num_formats = count;

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
