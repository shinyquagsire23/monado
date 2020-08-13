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
#include "util/u_logging.h"
#include "util/u_debug.h"

#include <xrt/xrt_config_have.h>
#include <xrt/xrt_config_os.h>
#include <xrt/xrt_handles.h>

#include "ogl/egl_api.h"
#include "ogl/ogl_api.h"

#include "client/comp_gl_client.h"
#include "client/comp_gl_eglimage_swapchain.h"

#include <inttypes.h>


static enum u_logging_level ll;

#define EGL_SC_TRACE(...) U_LOG_IFL_T(ll, __VA_ARGS__)
#define EGL_SC_DEBUG(...) U_LOG_IFL_D(ll, __VA_ARGS__)
#define EGL_SC_INFO(...) U_LOG_IFL_I(ll, __VA_ARGS__)
#define EGL_SC_WARN(...) U_LOG_IFL_W(ll, __VA_ARGS__)
#define EGL_SC_ERROR(...) U_LOG_IFL_E(ll, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(egl_swapchain_log,
                          "EGL_SWAPCHAIN_LOG",
                          U_LOGGING_WARN)


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
		EGL_SC_ERROR("Cannot convert GL format 0x%08" PRIx64
		             " to DRM FOURCC format!",
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
		EGL_SC_ERROR("Cannot convert GL format 0x%08" PRIx64
		             " to DRM FOURCC format!",
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
	ll = debug_get_log_option_egl_swapchain_log();

	if (xscn == NULL) {
		EGL_SC_ERROR("Native compositor is null");
		return NULL;
	}

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
	uint32_t format = gl_format_to_drm_fourcc(info->format);
	if (format == 0) {
		return NULL;
	}
	uint32_t row_pitch = 0;
	{
		uint32_t bpp = gl_format_to_bpp(info->format);
		uint32_t row_bits = bpp * info->width;
		row_pitch = row_bits / 8;
		if (row_pitch * 8 < row_bits) {
			// round up
			row_pitch += 1;
		}
		EGL_SC_INFO("Computed row pitch is %" PRIu32 " bytes: %" PRIu32
		            " bpp, %" PRIu32 " pixels wide",
		            row_pitch, bpp, info->width);
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
			EGL_SC_ERROR("eglGetNativeClientBufferANDROID failed");
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
#else
#error "need port"
#endif
		sc->egl_images[i] = eglCreateImageKHR(
		    sc->display, EGL_NO_CONTEXT, target, native_buffer, attrs);
		if (NULL == sc->egl_images[i]) {
			EGL_SC_ERROR("eglCreateImageKHR failed");
			client_gl_eglimage_swapchain_teardown_storage(sc);
			free(sc);
			return NULL;
		}
#if defined(XRT_OS_ANDROID)
		glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,
		                             sc->egl_images[i]);
#else
		//! @todo this should be glTexImage2D I think.
		glEGLImageTargetTexture2DOES(
		    info->array_size == 1 ? GL_TEXTURE_2D : GL_TEXTURE_2D_ARRAY,
		    sc->egl_images[i]);
#endif
	}

	return &sc->base.base.base;
}
