// Copyright 2019-2021, Collabora, Ltd.
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
#include "ogl/ogl_helpers.h"

#include "client/comp_gl_client.h"
#include "client/comp_egl_client.h"
#include "client/comp_gl_eglimage_swapchain.h"

#include <inttypes.h>

#ifdef XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER
#include <android/hardware_buffer.h>
#endif

static enum u_logging_level log_level;

#define EGL_SC_TRACE(...) U_LOG_IFL_T(log_level, __VA_ARGS__)
#define EGL_SC_DEBUG(...) U_LOG_IFL_D(log_level, __VA_ARGS__)
#define EGL_SC_INFO(...) U_LOG_IFL_I(log_level, __VA_ARGS__)
#define EGL_SC_WARN(...) U_LOG_IFL_W(log_level, __VA_ARGS__)
#define EGL_SC_ERROR(...) U_LOG_IFL_E(log_level, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(egl_swapchain_log, "EGL_SWAPCHAIN_LOG", U_LOGGING_WARN)

#define EGL_PROTECTED_CONTENT_EXT 0x32C0

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
client_gl_eglimage_swapchain_teardown_storage(struct client_gl_eglimage_swapchain *sc)
{
	uint32_t image_count = sc->base.base.base.image_count;
	if (image_count > 0) {
		glDeleteTextures(image_count, &sc->base.base.images[0]);
		U_ZERO_ARRAY(sc->base.base.images);
		for (uint32_t i = 0; i < image_count; ++i) {
			if (sc->egl_images[i] != NULL) {
				eglDestroyImageKHR(sc->display, &(sc->egl_images[i]));
			}
		}
		U_ZERO_ARRAY(sc->egl_images);
	}
}

static void
client_gl_eglimage_swapchain_destroy(struct xrt_swapchain *xsc)
{
	struct client_gl_eglimage_swapchain *sc = client_gl_eglimage_swapchain(xsc);

	client_gl_eglimage_swapchain_teardown_storage(sc);
	sc->base.base.base.image_count = 0;

	// Drop our reference, does NULL checking.
	xrt_swapchain_native_reference(&sc->base.xscn, NULL);

	free(sc);
}

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)

/*
 * See
 * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/drm/drm_fourcc.h
 * for the "source of truth" for this data.
 */

#define XRT_FOURCC(A, B, C, D) ((uint32_t)(A) | ((uint32_t)(B) << 8) | ((uint32_t)(C) << 16) | ((uint32_t)(D) << 24))

static inline uint32_t
gl_format_to_drm_fourcc(uint64_t format)
{
	switch (format) {

	case GL_RGBA8: return XRT_FOURCC('R', 'A', '2', '4');        /*DRM_FORMAT_RGBA8888*/
	case GL_SRGB8_ALPHA8: return XRT_FOURCC('R', 'A', '2', '4'); /*DRM_FORMAT_RGBA8888*/
	case GL_RGB10_A2: return XRT_FOURCC('A', 'B', '3', '0');     /*DRM_FORMAT_ABGR2101010*/
#if 0
	/* couldn't find a matching code? */
	case GL_RGBA16F:
#endif
	default: EGL_SC_ERROR("Cannot convert GL format 0x%08" PRIx64 " to DRM FOURCC format!", format); return 0;
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
	default: EGL_SC_ERROR("Cannot convert GL format 0x%08" PRIx64 " to DRM FOURCC format!", format); return 0;
	}
}
#endif // defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)


#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
static inline bool
is_gl_format_srgb(uint64_t format)
{
	switch (format) {
	case GL_RGB8: return false;
	case GL_SRGB8: return true; // sRGB
	case GL_RGBA8: return false;
	case GL_SRGB8_ALPHA8: return true; // sRGB
	case GL_RGB10_A2: return false;
	case GL_RGB16: return false;
	case GL_RGB16F: return false;
	case GL_RGBA16: return false;
	case GL_RGBA16F: return false;
	case GL_DEPTH_COMPONENT16: return false;
	case GL_DEPTH_COMPONENT32F: return false;
	case GL_DEPTH24_STENCIL8: return false;
	case GL_DEPTH32F_STENCIL8: return false;
	default: U_LOG_W("Cannot check GL format %" PRIu64 " for sRGB-ness!", format); return false;
	}
}
#endif // defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)


struct xrt_swapchain *
client_gl_eglimage_swapchain_create(struct xrt_compositor *xc,
                                    const struct xrt_swapchain_create_info *info,
                                    struct xrt_swapchain_native *xscn,
                                    struct client_gl_swapchain **out_sc)
{
	struct client_egl_compositor *ceglc = client_egl_compositor(xc);
	log_level = debug_get_log_option_egl_swapchain_log();

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
		EGL_SC_INFO("Computed row pitch is %" PRIu32 " bytes: %" PRIu32 " bpp, %" PRIu32 " pixels wide",
		            row_pitch, bpp, info->width);
	}
#endif // defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)

	struct xrt_swapchain *native_xsc = &xscn->base;

	struct client_gl_eglimage_swapchain *sc = U_TYPED_CALLOC(struct client_gl_eglimage_swapchain);
	sc->base.base.base.destroy = client_gl_eglimage_swapchain_destroy;
	sc->base.base.base.reference.count = 1;
	sc->base.base.base.image_count =
	    native_xsc->image_count; // Fetch the number of images from the native swapchain.
	sc->base.xscn = xscn;
	sc->display = ceglc->current.dpy;

	struct xrt_swapchain_gl *xscgl = &sc->base.base;

	glGenTextures(native_xsc->image_count, xscgl->images);

	GLuint binding_enum = 0;
	GLuint tex_target = 0;
	ogl_texture_target_for_swapchain_info(info, &tex_target, &binding_enum);
	sc->base.tex_target = tex_target;

	for (uint32_t i = 0; i < native_xsc->image_count; i++) {

		// Bind new texture name to the target.
		glBindTexture(tex_target, xscgl->images[i]);

		EGLClientBuffer native_buffer = NULL;

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
		// see
		// https://android.googlesource.com/platform/cts/+/master/tests/tests/nativehardware/jni/AHardwareBufferGLTest.cpp
		native_buffer = eglGetNativeClientBufferANDROID(xscn->images[i].handle);

		AHardwareBuffer_Desc desc;
		AHardwareBuffer_describe(xscn->images[i].handle, &desc);

		if (NULL == native_buffer) {
			EGL_SC_ERROR("eglGetNativeClientBufferANDROID failed");
			client_gl_eglimage_swapchain_teardown_storage(sc);
			free(sc);
			return NULL;
		}
		EGLint attrs[] = {
		    EGL_IMAGE_PRESERVED_KHR,
		    EGL_TRUE,
		    EGL_PROTECTED_CONTENT_EXT,
		    (desc.usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT) ? EGL_TRUE : EGL_FALSE,
		    EGL_NONE,
		    EGL_NONE,
		    EGL_NONE,
		};

		EGL_SC_INFO("EGL_PROTECTED_CONTENT_EXT %s",
		            (desc.usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT) ? "TRUE" : "FALSE");

		if (is_gl_format_srgb(info->format)) {
			attrs[4] = EGL_GL_COLORSPACE_KHR;
			attrs[5] = EGL_GL_COLORSPACE_SRGB_KHR;
		}

		EGLenum source = EGL_NATIVE_BUFFER_ANDROID;
#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
		EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR,
		                  EGL_TRUE,
		                  EGL_WIDTH,
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
		EGLenum source = EGL_LINUX_DMA_BUF_EXT;
#else
#error "need port"
#endif
		sc->egl_images[i] = eglCreateImageKHR(sc->display, EGL_NO_CONTEXT, source, native_buffer, attrs);
		if (EGL_NO_IMAGE_KHR == sc->egl_images[i]) {
			EGL_SC_ERROR("eglCreateImageKHR failed");
			client_gl_eglimage_swapchain_teardown_storage(sc);
			free(sc);
			return NULL;
		}
		/*!
		 * @todo this matches the behavior of the Google test, but is
		 * not itself tested or fully rationalized.
		 *
		 * Also, glEGLImageTargetTexStorageEXT was added in Android
		 * platform 28, so fairly recently.
		 */
		if (GLAD_GL_EXT_EGL_image_storage && glEGLImageTargetTexStorageEXT) {
			glEGLImageTargetTexStorageEXT(tex_target, sc->egl_images[i], NULL);
		} else if (GLAD_GL_OES_EGL_image_external || GLAD_GL_OES_EGL_image_external_essl3) {
			glEGLImageTargetTexture2DOES(tex_target, sc->egl_images[i]);
		}
	}

	*out_sc = &sc->base;
	return &sc->base.base.base;
}
