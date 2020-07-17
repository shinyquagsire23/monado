// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenGL client side glue to swapchain implementation -
 * EGLImageKHR-backed.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "util/u_misc.h"

#include <xrt/xrt_config_have.h>
#include <xrt/xrt_config_os.h>

#if defined(XRT_HAVE_EGL)
#include "ogl/egl_api.h"
#endif
#if defined(XRT_HAVE_OPENGL) || defined(XRT_HAVE_OPENGLES)
#include "ogl/ogl_api.h"
#endif

#include "client/comp_gl_client.h"
#include "client/comp_gl_eglimage_swapchain.h"

#include <inttypes.h>

/*!
 * Down-cast helper.
 * @private @memberof client_gl_eglimage_swapchain
 */
static inline struct client_gl_eglimage_swapchain *
client_gl_eglimage_swapchain(struct xrt_swapchain *xsc)
{
	return (struct client_gl_eglimage_swapchain *)xsc;
}

/*
 *
 * Swapchain functions.
 *
 */

// This is shared between "destroy" and an error cleanup
static void
client_gl_eglimage_swapchain_teardown_storage(
    struct client_gl_eglimage_swapchain *sc)
{
	uint32_t num_images = sc->base.base.base.num_images;
	if (num_images > 0) {
		glDeleteTextures(num_images, &sc->base.base.images[0]);
		U_ZERO_ARRAY(sc->base.base.images);
		for (uint32_t i = 0; i < num_images; ++i) {
			if (sc->egl_images[i] != NULL) {
				eglDestroyImageKHR(sc->display,
				                   &(sc->egl_images[i]));
			}
		}
		U_ZERO_ARRAY(sc->egl_images);
	}
}

static void
client_gl_eglimage_swapchain_destroy(struct xrt_swapchain *xsc)
{
	struct client_gl_eglimage_swapchain *sc =
	    client_gl_eglimage_swapchain(xsc);

	client_gl_eglimage_swapchain_teardown_storage(sc);
	sc->base.base.base.num_images = 0;

	// Destroy the native swapchain as well.
	xrt_swapchain_destroy((struct xrt_swapchain **)&sc->base.xscn);

	free(sc);
}

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)

/*
 * See
 * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/drm/drm_fourcc.h
 * for the "source of truth" for this data.
 */

#define XRT_FOURCC(A, B, C, D)                                                 \
	((uint32_t)(A) | ((uint32_t)(B) << 8) | ((uint32_t)(C) << 16) |        \
	 ((uint32_t)(D) << 24))

static inline uint32_t
gl_format_to_drm_fourcc(uint64_t format)
{
	switch (format) {

	case GL_RGBA8:
		return XRT_FOURCC('R', 'A', '2', '4'); /*DRM_FORMAT_RGBA8888*/
	case GL_SRGB8_ALPHA8:
		return XRT_FOURCC('R', 'A', '2', '4'); /*DRM_FORMAT_RGBA8888*/
	case GL_RGB10_A2:
		return XRT_FOURCC('A', 'B', '3',
		                  '0'); /*DRM_FORMAT_ABGR2101010*/
#if 0
	/* couldn't find a matching code? */
	case GL_RGBA16F:
#endif
	default:
		printf("Cannot convert VK format 0x%016" PRIx64
		       " to DRM FOURCC format!\n",
		       format);
		return 0;
	}
}
static inline uint32_t
gl_format_to_bpp(uint64_t format)
{
	switch (format) {

	case GL_RGBA8: return 32;        /*DRM_FORMAT_RGBA8888*/
	case GL_SRGB8_ALPHA8: return 32; /*DRM_FORMAT_RGBA8888*/
	case GL_RGB10_A2: return 32;     /*DRM_FORMAT_ABGR2101010*/
#if 0
	/* couldn't find a matching code? */
	case GL_RGBA16F:
#endif
	default:
		printf("Cannot convert VK format 0x%016" PRIx64
		       " to DRM FOURCC format!\n",
		       format);
		return 0;
	}
}
#endif // defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)

struct xrt_swapchain *
client_gl_eglimage_swapchain_create(
    struct xrt_compositor *xc,
    const struct xrt_swapchain_create_info *info,
    struct xrt_swapchain_native *xscn,
    struct client_gl_swapchain **out_sc)
{
	if (xscn == NULL) {
		return NULL;
	}

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
	uint32_t format = gl_format_to_drm_fourcc(info->format);
	if (format == 0) {
		return NULL;
	}
	uint32_t row_bits = gl_format_to_bpp(info->format) * info->width;
	uint32_t row_pitch = row_bits / 8;
	if (row_pitch * 8 < row_bits) {
		// round up
		row_pitch += 1;
	}
#endif // defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)

	struct xrt_swapchain *native_xsc = &xscn->base;

	struct client_gl_eglimage_swapchain *sc =
	    U_TYPED_CALLOC(struct client_gl_eglimage_swapchain);
	struct xrt_swapchain_gl *xscgl = &sc->base.base;
	struct xrt_swapchain *client_xsc = &xscgl->base;
	client_xsc->destroy = client_gl_eglimage_swapchain_destroy;
	// Fetch the number of images from the native swapchain.
	client_xsc->num_images = native_xsc->num_images;
	sc->base.xscn = xscn;

	sc->display = eglGetCurrentDisplay();

	glGenTextures(native_xsc->num_images, xscgl->images);



	for (uint32_t i = 0; i < native_xsc->num_images; i++) {
#ifdef XRT_OS_ANDROID
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, xscgl->images[i]);
#else
		glBindTexture(info->array_size == 1 ? GL_TEXTURE_2D
		                                    : GL_TEXTURE_2D_ARRAY,
		              xscgl->images[i]);
#endif

		EGLClientBuffer native_buffer = NULL;

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
		native_buffer =
		    eglGetNativeClientBufferANDROID(xscn->images[i].handle);

		if (NULL == native_buffer) {
			client_gl_eglimage_swapchain_teardown_storage(sc);
			free(sc);
			return NULL;
		}
		EGLint attrs[] = {EGL_NONE};
		EGLenum target = EGL_NATIVE_BUFFER_ANDROID;
#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
		EGLint attrs[] = {EGL_WIDTH,
		                  info->width,
		                  EGL_HEIGHT,
		                  info->height,
		                  EGL_LINUX_DRM_FOURCC_EXT,
		                  format,
		                  EGL_DMA_BUF_PLANE0_FD_EXT,
		                  xscn->images[i].handle,
		                  EGL_DMA_BUF_PLANE0_OFFSET_EXT,
		                  0,
		                  EGL_DMA_BUF_PLANE0_PITCH_EXT,
		                  row_pitch,
		                  EGL_NONE};
		EGLenum target = EGL_LINUX_DMA_BUF_EXT;
#endif
		sc->egl_images[i] = eglCreateImageKHR(
		    sc->display, EGL_NO_CONTEXT, target, native_buffer, attrs);
		if (NULL == sc->egl_images[i]) {
			client_gl_eglimage_swapchain_teardown_storage(sc);
			free(sc);
			return NULL;
		}
		glEGLImageTargetTexture2DOES(
		    info->array_size == 1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_ARRAY,
		    sc->egl_images[i]);
	}

	return &sc->base.base.base;
}
