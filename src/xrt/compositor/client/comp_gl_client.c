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

#include <xrt/xrt_config_have.h>
#if defined(XRT_HAVE_EGL)
#include "ogl/egl_api.h"
#endif
#if defined(XRT_HAVE_OPENGL) || defined(XRT_HAVE_OPENGLES)
#include "ogl/ogl_api.h"
#endif

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

static xrt_result_t
client_gl_swapchain_acquire_image(struct xrt_swapchain *xsc,
                                  uint32_t *out_index)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	// Pipe down call into native swapchain.
	return xrt_swapchain_acquire_image(&sc->xscn->base, out_index);
}

static xrt_result_t
client_gl_swapchain_wait_image(struct xrt_swapchain *xsc,
                               uint64_t timeout,
                               uint32_t index)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	// Pipe down call into native swapchain.
	return xrt_swapchain_wait_image(&sc->xscn->base, timeout, index);
}

static xrt_result_t
client_gl_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	// Pipe down call into native swapchain.
	return xrt_swapchain_release_image(&sc->xscn->base, index);
}


/*
 *
 * Compositor functions.
 *
 */

static xrt_result_t
client_gl_compositor_prepare_session(struct xrt_compositor *xc,
                                     struct xrt_session_prepare_info *xspi)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_prepare_session(&c->xcn->base, xspi);
}


static xrt_result_t
client_gl_compositor_begin_session(struct xrt_compositor *xc,
                                   enum xrt_view_type type)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_begin_session(&c->xcn->base, type);
}

static xrt_result_t
client_gl_compositor_end_session(struct xrt_compositor *xc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_end_session(&c->xcn->base);
}

static xrt_result_t
client_gl_compositor_wait_frame(struct xrt_compositor *xc,
                                int64_t *out_frame_id,
                                uint64_t *predicted_display_time,
                                uint64_t *predicted_display_period)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_wait_frame(&c->xcn->base, out_frame_id,
	                           predicted_display_time,
	                           predicted_display_period);
}

static xrt_result_t
client_gl_compositor_begin_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_begin_frame(&c->xcn->base, frame_id);
}

static xrt_result_t
client_gl_compositor_discard_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_discard_frame(&c->xcn->base, frame_id);
}

static xrt_result_t
client_gl_compositor_layer_begin(struct xrt_compositor *xc,
                                 int64_t frame_id,
                                 enum xrt_blend_mode env_blend_mode)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	return xrt_comp_layer_begin(&c->xcn->base, frame_id, env_blend_mode);
}

static xrt_result_t
client_gl_compositor_layer_stereo_projection(struct xrt_compositor *xc,
                                             struct xrt_device *xdev,
                                             struct xrt_swapchain *l_xsc,
                                             struct xrt_swapchain *r_xsc,
                                             struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *l_xscn, *r_xscn;

	assert(data->type == XRT_LAYER_STEREO_PROJECTION);

	l_xscn = &client_gl_swapchain(l_xsc)->xscn->base;
	r_xscn = &client_gl_swapchain(r_xsc)->xscn->base;
	data->flip_y = true;

	return xrt_comp_layer_stereo_projection(&c->xcn->base, xdev, l_xscn,
	                                        r_xscn, data);
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

	xscfb = &client_gl_swapchain(xsc)->xscn->base;
	data->flip_y = true;

	return xrt_comp_layer_quad(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_gl_compositor_layer_cube(struct xrt_compositor *xc,
                                struct xrt_device *xdev,
                                struct xrt_swapchain *xsc,
                                struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_CUBE);

	xscfb = &client_gl_swapchain(xsc)->xscn->base;
	data->flip_y = true;

	return xrt_comp_layer_cube(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_gl_compositor_layer_cylinder(struct xrt_compositor *xc,
                                    struct xrt_device *xdev,
                                    struct xrt_swapchain *xsc,
                                    struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_CYLINDER);

	xscfb = &client_gl_swapchain(xsc)->xscn->base;
	data->flip_y = true;

	return xrt_comp_layer_cylinder(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_gl_compositor_layer_equirect(struct xrt_compositor *xc,
                                    struct xrt_device *xdev,
                                    struct xrt_swapchain *xsc,
                                    struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_EQUIRECT);

	xscfb = &client_gl_swapchain(xsc)->xscn->base;
	data->flip_y = true;

	return xrt_comp_layer_equirect(&c->xcn->base, xdev, xscfb, data);
}

static xrt_result_t
client_gl_compositor_layer_commit(struct xrt_compositor *xc, int64_t frame_id)
{
	//! @hack: The swapchain images should have been externally synchronized
	glFlush();

	struct client_gl_compositor *c = client_gl_compositor(xc);

	return xrt_comp_layer_commit(&c->xcn->base, frame_id);
}

static int64_t
gl_format_to_vk(int64_t format)
{
	switch (format) {
	case GL_RGBA8: return 37 /*VK_FORMAT_R8G8B8A8_UNORM*/;
	case GL_SRGB8_ALPHA8: return 43 /*VK_FORMAT_R8G8B8A8_SRGB*/;
	case GL_RGB10_A2: return 64 /*VK_FORMAT_A2B10G10R10_UNORM_PACK32*/;
	case GL_RGBA16F: return 97 /*VK_FORMAT_R16G16B16A16_SFLOAT*/;
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
	case 64 /*VK_FORMAT_A2B10G10R10_UNORM_PACK32*/: return GL_RGB10_A2;
	case 97 /*VK_FORMAT_R16G16B16A16_SFLOAT*/: return GL_RGBA16F;
	default:
		printf("Cannot convert VK format 0x%016" PRIx64
		       " to GL format!\n",
		       format);
		return 0;
	}
}

static xrt_result_t
client_gl_swapchain_create(struct xrt_compositor *xc,
                           const struct xrt_swapchain_create_info *info,
                           struct xrt_swapchain **out_xsc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	xrt_result_t xret = XRT_SUCCESS;

	if (info->array_size > 1) {
		const char *version_str = (const char *)glGetString(GL_VERSION);
		if (strstr(version_str, "OpenGL ES 2.") == version_str) {
			fprintf(stderr,
			        "%s - only one array layer is supported with "
			        "OpenGL ES 2\n",
			        __func__);
			return XRT_ERROR_OPENGL;
		}
	}

	int64_t vk_format = gl_format_to_vk(info->format);
	if (vk_format == 0) {
		fprintf(stderr, "%s - Invalid format!\n", __func__);
		return XRT_ERROR_VULKAN;
	}

	struct xrt_swapchain_create_info xinfo = *info;
	xinfo.format = vk_format;
	struct xrt_swapchain_native *xscn = NULL;
	xret = xrt_comp_native_create_swapchain(c->xcn, &xinfo, &xscn);


	if (xret != XRT_SUCCESS) {
		return xret;
	}
	assert(xscn != NULL);

	// Save texture binding
	GLuint prev_texture = 0;
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	const GLuint binding_enum = GL_TEXTURE_BINDING_EXTERNAL_OES;
#else
	const GLuint binding_enum = info->array_size == 1
	                                ? GL_TEXTURE_BINDING_2D
	                                : GL_TEXTURE_BINDING_2D_ARRAY;
#endif

	glGetIntegerv(binding_enum, (GLint *)&prev_texture);

	struct xrt_swapchain *xsc = &xscn->base;

	struct client_gl_swapchain *sc = NULL;
	if (NULL == c->create_swapchain(xc, info, xscn, &sc)) {
		xrt_swapchain_destroy(&xsc);
		return XRT_ERROR_OPENGL;
	}
	if (NULL == sc->base.base.acquire_image) {
		sc->base.base.acquire_image = client_gl_swapchain_acquire_image;
	}
	if (NULL == sc->base.base.wait_image) {
		sc->base.base.wait_image = client_gl_swapchain_wait_image;
	}
	if (NULL == sc->base.base.release_image) {
		sc->base.base.release_image = client_gl_swapchain_release_image;
	}
	// Fetch the number of images from the native swapchain.
	sc->base.base.num_images = xsc->num_images;
	sc->xscn = xscn;

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	const GLuint binding_target_enum = GL_TEXTURE_EXTERNAL_OES;
#else
	const GLuint binding_target_enum =
	    info->array_size == 1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_ARRAY;
#endif
	glBindTexture(binding_target_enum, prev_texture);

	*out_xsc = &sc->base.base;
	return XRT_SUCCESS;
}

static xrt_result_t
client_gl_compositor_poll_events(struct xrt_compositor *xc,
                                 union xrt_compositor_event *out_xce)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	// Pipe down call into native compositor.
	return xrt_comp_poll_events(&c->xcn->base, out_xce);
}

static void
client_gl_compositor_destroy(struct xrt_compositor *xc)
{
	assert(!"Destroy should be implemented by the winsys code that uses the GL code.");
}

bool
client_gl_compositor_init(struct client_gl_compositor *c,
                          struct xrt_compositor_native *xcn,
                          client_gl_swapchain_create_func create_swapchain)
{
	c->base.base.create_swapchain = client_gl_swapchain_create;
	c->base.base.prepare_session = client_gl_compositor_prepare_session;
	c->base.base.begin_session = client_gl_compositor_begin_session;
	c->base.base.end_session = client_gl_compositor_end_session;
	c->base.base.wait_frame = client_gl_compositor_wait_frame;
	c->base.base.begin_frame = client_gl_compositor_begin_frame;
	c->base.base.discard_frame = client_gl_compositor_discard_frame;
	c->base.base.layer_begin = client_gl_compositor_layer_begin;
	c->base.base.layer_stereo_projection =
	    client_gl_compositor_layer_stereo_projection;
	c->base.base.layer_quad = client_gl_compositor_layer_quad;
	c->base.base.layer_cube = client_gl_compositor_layer_cube;
	c->base.base.layer_cylinder = client_gl_compositor_layer_cylinder;
	c->base.base.layer_equirect = client_gl_compositor_layer_equirect;
	c->base.base.layer_commit = client_gl_compositor_layer_commit;
	c->base.base.destroy = client_gl_compositor_destroy;
	c->base.base.poll_events = client_gl_compositor_poll_events;
	c->create_swapchain = create_swapchain;
	c->xcn = xcn;

	// Passthrough our formats from the native compositor to the client.
	size_t count = 0;
	for (uint32_t i = 0; i < xcn->base.info.num_formats; i++) {
		int64_t f = vk_format_to_gl(xcn->base.info.formats[i]);
		if (f == 0) {
			continue;
		}

		c->base.base.info.formats[count++] = f;
	}
	c->base.base.info.num_formats = count;

	return true;
}
