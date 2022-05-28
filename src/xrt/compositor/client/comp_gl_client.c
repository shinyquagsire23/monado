// Copyright 2019-2021, Collabora, Ltd.
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

#include "xrt/xrt_config_os.h"
#include "util/u_misc.h"

#include <xrt/xrt_config_have.h>
#if defined(XRT_HAVE_EGL)
#include "ogl/egl_api.h"
#endif
#if defined(XRT_HAVE_OPENGL) || defined(XRT_HAVE_OPENGLES)
#include "ogl/ogl_api.h"
#endif

#include "ogl/ogl_helpers.h"

#include "client/comp_gl_client.h"

#include "util/u_logging.h"
#include "util/u_trace_marker.h"

#include <inttypes.h>


/*
 *
 * Helpers.
 *
 */

/*!
 * Down-cast helper.
 * @private @memberof client_gl_swapchain
 */
static inline struct client_gl_swapchain *
client_gl_swapchain(struct xrt_swapchain *xsc)
{
	return (struct client_gl_swapchain *)xsc;
}

static int64_t
gl_format_to_vk(int64_t format)
{
	switch (format) {
	case GL_RGB8: return 23 /*VK_FORMAT_R8G8B8_UNORM*/; // Should not be used, colour precision.
	case GL_SRGB8: return 29 /*VK_FORMAT_R8G8B8_SRGB*/;
	case GL_RGBA8: return 37 /*VK_FORMAT_R8G8B8A8_UNORM*/; // Should not be used, colour precision.
	case GL_SRGB8_ALPHA8: return 43 /*VK_FORMAT_R8G8B8A8_SRGB*/;
	case GL_RGB10_A2: return 64 /*VK_FORMAT_A2B10G10R10_UNORM_PACK32*/;
	case GL_RGB16: return 84 /*VK_FORMAT_R16G16B16_UNORM*/;
	case GL_RGB16F: return 90 /*VK_FORMAT_R16G16B16_SFLOAT*/;
	case GL_RGBA16: return 91 /*VK_FORMAT_R16G16B16A16_UNORM*/;
	case GL_RGBA16F: return 97 /*VK_FORMAT_R16G16B16A16_SFLOAT*/;
	case GL_DEPTH_COMPONENT16: return 124 /*VK_FORMAT_D16_UNORM*/;
	case GL_DEPTH_COMPONENT32F: return 126 /*VK_FORMAT_D32_SFLOAT*/;
	case GL_DEPTH24_STENCIL8: return 129 /*VK_FORMAT_D24_UNORM_S8_UINT*/;
	case GL_DEPTH32F_STENCIL8: return 130 /*VK_FORMAT_D32_SFLOAT_S8_UINT*/;
	default: U_LOG_W("Cannot convert GL format %" PRIu64 " to VK format!", format); return 0;
	}
}

static int64_t
vk_format_to_gl(int64_t format)
{
	switch (format) {
	case 4 /*   VK_FORMAT_R5G6B5_UNORM_PACK16      */: return 0;       // GL_RGB565?
	case 23 /*  VK_FORMAT_R8G8B8_UNORM             */: return GL_RGB8; // Should not be used, colour precision.
	case 29 /*  VK_FORMAT_R8G8B8_SRGB              */: return GL_SRGB8;
	case 30 /*  VK_FORMAT_B8G8R8_UNORM             */: return 0;
	case 37 /*  VK_FORMAT_R8G8B8A8_UNORM           */: return GL_RGBA8; // Should not be used, colour precision.
	case 43 /*  VK_FORMAT_R8G8B8A8_SRGB            */: return GL_SRGB8_ALPHA8;
	case 44 /*  VK_FORMAT_B8G8R8A8_UNORM           */: return 0;
	case 50 /*  VK_FORMAT_B8G8R8A8_SRGB            */: return 0;
	case 64 /*  VK_FORMAT_A2B10G10R10_UNORM_PACK32 */: return GL_RGB10_A2;
	case 84 /*  VK_FORMAT_R16G16B16_UNORM          */: return GL_RGB16;
	case 90 /*  VK_FORMAT_R16G16B16_SFLOAT         */: return GL_RGB16F;
	case 91 /*  VK_FORMAT_R16G16B16A16_UNORM       */: return GL_RGBA16;
	case 97 /*  VK_FORMAT_R16G16B16A16_SFLOAT      */: return GL_RGBA16F;
	case 124 /* VK_FORMAT_D16_UNORM                */: return GL_DEPTH_COMPONENT16;
	case 125 /* VK_FORMAT_X8_D24_UNORM_PACK32      */: return 0; // GL_DEPTH_COMPONENT24?
	case 126 /* VK_FORMAT_D32_SFLOAT               */: return GL_DEPTH_COMPONENT32F;
	case 127 /* VK_FORMAT_S8_UINT                  */: return 0; // GL_STENCIL_INDEX8?
	case 129 /* VK_FORMAT_D24_UNORM_S8_UINT        */: return GL_DEPTH24_STENCIL8;
	case 130 /* VK_FORMAT_D32_SFLOAT_S8_UINT       */: return GL_DEPTH32F_STENCIL8;
	default: U_LOG_W("Cannot convert VK format %" PRIu64 " to GL format!", format); return 0;
	}
}

/*!
 * Called with the right context made current.
 */
static xrt_graphics_sync_handle_t
handle_fencing_or_finish(struct client_gl_compositor *c)
{
	xrt_graphics_sync_handle_t sync_handle = XRT_GRAPHICS_SYNC_HANDLE_INVALID;
	xrt_result_t xret = XRT_SUCCESS;

	if (c->insert_fence != NULL) {
		COMP_TRACE_IDENT(insert_fence);

		xret = c->insert_fence(&c->base.base, &sync_handle);
		if (xret != XRT_SUCCESS) {
			U_LOG_E("Failed to insert a fence");
		}
	}

	// Fallback to glFinish if we haven't inserted a fence.
	if (sync_handle == XRT_GRAPHICS_SYNC_HANDLE_INVALID) {
		COMP_TRACE_IDENT(glFinish);

		glFinish();
	}

	return sync_handle;
}


/*
 *
 * Swapchain functions.
 *
 */

static xrt_result_t
client_gl_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *out_index)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	// Pipe down call into native swapchain.
	return xrt_swapchain_acquire_image(&sc->xscn->base, out_index);
}

static xrt_result_t
client_gl_swapchain_wait_image(struct xrt_swapchain *xsc, uint64_t timeout_ns, uint32_t index)
{
	struct client_gl_swapchain *sc = client_gl_swapchain(xsc);

	// Pipe down call into native swapchain.
	return xrt_swapchain_wait_image(&sc->xscn->base, timeout_ns, index);
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
client_gl_compositor_begin_session(struct xrt_compositor *xc, enum xrt_view_type type)
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
	return xrt_comp_wait_frame(&c->xcn->base, out_frame_id, predicted_display_time, predicted_display_period);
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
                                 uint64_t display_time_ns,
                                 enum xrt_blend_mode env_blend_mode)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	return xrt_comp_layer_begin(&c->xcn->base, frame_id, display_time_ns, env_blend_mode);
}

static xrt_result_t
client_gl_compositor_layer_stereo_projection(struct xrt_compositor *xc,
                                             struct xrt_device *xdev,
                                             struct xrt_swapchain *l_xsc,
                                             struct xrt_swapchain *r_xsc,
                                             const struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *l_xscn;
	struct xrt_swapchain *r_xscn;

	assert(data->type == XRT_LAYER_STEREO_PROJECTION);

	l_xscn = &client_gl_swapchain(l_xsc)->xscn->base;
	r_xscn = &client_gl_swapchain(r_xsc)->xscn->base;

	struct xrt_layer_data d = *data;
	d.flip_y = !d.flip_y;

	return xrt_comp_layer_stereo_projection(&c->xcn->base, xdev, l_xscn, r_xscn, &d);
}

static xrt_result_t
client_gl_compositor_layer_stereo_projection_depth(struct xrt_compositor *xc,
                                                   struct xrt_device *xdev,
                                                   struct xrt_swapchain *l_xsc,
                                                   struct xrt_swapchain *r_xsc,
                                                   struct xrt_swapchain *l_d_xsc,
                                                   struct xrt_swapchain *r_d_xsc,
                                                   const struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *l_xscn;
	struct xrt_swapchain *r_xscn;
	struct xrt_swapchain *l_d_xscn;
	struct xrt_swapchain *r_d_xscn;

	assert(data->type == XRT_LAYER_STEREO_PROJECTION_DEPTH);

	l_xscn = &client_gl_swapchain(l_xsc)->xscn->base;
	r_xscn = &client_gl_swapchain(r_xsc)->xscn->base;
	l_d_xscn = &client_gl_swapchain(l_d_xsc)->xscn->base;
	r_d_xscn = &client_gl_swapchain(r_d_xsc)->xscn->base;

	struct xrt_layer_data d = *data;
	d.flip_y = !d.flip_y;

	return xrt_comp_layer_stereo_projection_depth(&c->xcn->base, xdev, l_xscn, r_xscn, l_d_xscn, r_d_xscn, &d);
}

static xrt_result_t
client_gl_compositor_layer_quad(struct xrt_compositor *xc,
                                struct xrt_device *xdev,
                                struct xrt_swapchain *xsc,
                                const struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_QUAD);

	xscfb = &client_gl_swapchain(xsc)->xscn->base;

	struct xrt_layer_data d = *data;
	d.flip_y = !d.flip_y;

	return xrt_comp_layer_quad(&c->xcn->base, xdev, xscfb, &d);
}

static xrt_result_t
client_gl_compositor_layer_cube(struct xrt_compositor *xc,
                                struct xrt_device *xdev,
                                struct xrt_swapchain *xsc,
                                const struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_CUBE);

	xscfb = &client_gl_swapchain(xsc)->xscn->base;

	struct xrt_layer_data d = *data;
	d.flip_y = !d.flip_y;

	return xrt_comp_layer_cube(&c->xcn->base, xdev, xscfb, &d);
}

static xrt_result_t
client_gl_compositor_layer_cylinder(struct xrt_compositor *xc,
                                    struct xrt_device *xdev,
                                    struct xrt_swapchain *xsc,
                                    const struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_CYLINDER);

	xscfb = &client_gl_swapchain(xsc)->xscn->base;

	struct xrt_layer_data d = *data;
	d.flip_y = !d.flip_y;

	return xrt_comp_layer_cylinder(&c->xcn->base, xdev, xscfb, &d);
}

static xrt_result_t
client_gl_compositor_layer_equirect1(struct xrt_compositor *xc,
                                     struct xrt_device *xdev,
                                     struct xrt_swapchain *xsc,
                                     const struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_EQUIRECT1);

	xscfb = &client_gl_swapchain(xsc)->xscn->base;

	struct xrt_layer_data d = *data;
	d.flip_y = !d.flip_y;

	return xrt_comp_layer_equirect1(&c->xcn->base, xdev, xscfb, &d);
}

static xrt_result_t
client_gl_compositor_layer_equirect2(struct xrt_compositor *xc,
                                     struct xrt_device *xdev,
                                     struct xrt_swapchain *xsc,
                                     const struct xrt_layer_data *data)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	struct xrt_swapchain *xscfb;

	assert(data->type == XRT_LAYER_EQUIRECT2);

	xscfb = &client_gl_swapchain(xsc)->xscn->base;

	struct xrt_layer_data d = *data;
	d.flip_y = !d.flip_y;

	return xrt_comp_layer_equirect2(&c->xcn->base, xdev, xscfb, &d);
}

static xrt_result_t
client_gl_compositor_layer_commit(struct xrt_compositor *xc, int64_t frame_id, xrt_graphics_sync_handle_t sync_handle)
{
	COMP_TRACE_MARKER();

	struct client_gl_compositor *c = client_gl_compositor(xc);

	// We make the sync object, not st/oxr which is our user.
	assert(!xrt_graphics_sync_handle_is_valid(sync_handle));

	sync_handle = XRT_GRAPHICS_SYNC_HANDLE_INVALID;

	xrt_result_t xret = c->context_begin(xc);
	if (xret == XRT_SUCCESS) {
		sync_handle = handle_fencing_or_finish(c);
		c->context_end(xc);
	}

	COMP_TRACE_IDENT(layer_commit);

	return xrt_comp_layer_commit(&c->xcn->base, frame_id, sync_handle);
}

static xrt_result_t
client_gl_compositor_get_swapchain_create_properties(struct xrt_compositor *xc,
                                                     const struct xrt_swapchain_create_info *info,
                                                     struct xrt_swapchain_create_properties *xsccp)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);

	return xrt_comp_get_swapchain_create_properties(&c->xcn->base, info, xsccp);
}

static xrt_result_t
client_gl_swapchain_create(struct xrt_compositor *xc,
                           const struct xrt_swapchain_create_info *info,
                           struct xrt_swapchain **out_xsc)
{
	struct client_gl_compositor *c = client_gl_compositor(xc);
	xrt_result_t xret = XRT_SUCCESS;

	xret = c->context_begin(xc);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	if (info->array_size > 1) {
		const char *version_str = (const char *)glGetString(GL_VERSION);
		if (strstr(version_str, "OpenGL ES 2.") == version_str) {
			U_LOG_E("Only one array layer is supported with OpenGL ES 2");
			c->context_end(xc);
			return XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED;
		}
	}

	int64_t vk_format = gl_format_to_vk(info->format);
	if (vk_format == 0) {
		U_LOG_E("Invalid format!");
		c->context_end(xc);
		return XRT_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	}

	struct xrt_swapchain_create_info xinfo = *info;
	xinfo.format = vk_format;
	struct xrt_swapchain_native *xscn = NULL; // Has to be NULL.
	xret = xrt_comp_native_create_swapchain(c->xcn, &xinfo, &xscn);


	if (xret != XRT_SUCCESS) {
		c->context_end(xc);
		return xret;
	}
	assert(xscn != NULL);

	// Save texture binding
	GLint prev_texture = 0;
	GLuint binding_enum = 0;
	GLuint tex_target = 0;
	ogl_texture_target_for_swapchain_info(&xinfo, &tex_target, &binding_enum);

	glGetIntegerv(binding_enum, &prev_texture);

	struct xrt_swapchain *xsc = &xscn->base;

	struct client_gl_swapchain *sc = NULL;
	if (NULL == c->create_swapchain(xc, info, xscn, &sc)) {
		// Drop our reference, does NULL checking.
		xrt_swapchain_reference(&xsc, NULL);
		c->context_end(xc);
		return XRT_ERROR_OPENGL;
	}

	if (sc == NULL) {
		U_LOG_E("Could not create OpenGL swapchain.");
		c->context_end(xc);
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
	sc->base.base.image_count = xsc->image_count;
	sc->xscn = xscn;

	glBindTexture(tex_target, prev_texture);

	c->context_end(xc);

	*out_xsc = &sc->base.base;
	return XRT_SUCCESS;
}

static xrt_result_t
client_gl_compositor_poll_events(struct xrt_compositor *xc, union xrt_compositor_event *out_xce)
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


/*
 *
 * 'Exported' functions.
 *
 */

void
client_gl_compositor_close(struct client_gl_compositor *c)
{
	os_mutex_destroy(&c->context_mutex);
}

bool
client_gl_compositor_init(struct client_gl_compositor *c,
                          struct xrt_compositor_native *xcn,
                          client_gl_context_begin_func_t context_begin,
                          client_gl_context_end_func_t context_end,
                          client_gl_swapchain_create_func_t create_swapchain,
                          client_gl_insert_fence_func_t insert_fence)
{
	assert(context_begin != NULL);
	assert(context_end != NULL);

	c->base.base.get_swapchain_create_properties = client_gl_compositor_get_swapchain_create_properties;
	c->base.base.create_swapchain = client_gl_swapchain_create;
	c->base.base.begin_session = client_gl_compositor_begin_session;
	c->base.base.end_session = client_gl_compositor_end_session;
	c->base.base.wait_frame = client_gl_compositor_wait_frame;
	c->base.base.begin_frame = client_gl_compositor_begin_frame;
	c->base.base.discard_frame = client_gl_compositor_discard_frame;
	c->base.base.layer_begin = client_gl_compositor_layer_begin;
	c->base.base.layer_stereo_projection = client_gl_compositor_layer_stereo_projection;
	c->base.base.layer_stereo_projection_depth = client_gl_compositor_layer_stereo_projection_depth;
	c->base.base.layer_quad = client_gl_compositor_layer_quad;
	c->base.base.layer_cube = client_gl_compositor_layer_cube;
	c->base.base.layer_cylinder = client_gl_compositor_layer_cylinder;
	c->base.base.layer_equirect1 = client_gl_compositor_layer_equirect1;
	c->base.base.layer_equirect2 = client_gl_compositor_layer_equirect2;
	c->base.base.layer_commit = client_gl_compositor_layer_commit;
	c->base.base.destroy = client_gl_compositor_destroy;
	c->base.base.poll_events = client_gl_compositor_poll_events;
	c->context_begin = context_begin;
	c->context_end = context_end;
	c->create_swapchain = create_swapchain;
	c->insert_fence = insert_fence;
	c->xcn = xcn;

	// Passthrough our formats from the native compositor to the client.
	uint32_t count = 0;

	// Make sure that we can fit all formats in the destination.
	static_assert(ARRAY_SIZE(xcn->base.info.formats) == ARRAY_SIZE(c->base.base.info.formats), "mismatch");

	for (uint32_t i = 0; i < xcn->base.info.format_count; i++) {
		int64_t f = vk_format_to_gl(xcn->base.info.formats[i]);
		if (f == 0) {
			continue;
		}

		c->base.base.info.formats[count++] = f;
	}
	c->base.base.info.format_count = count;

	os_mutex_init(&c->context_mutex);

	return true;
}
