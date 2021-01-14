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
#include "ogl/ogl_helpers.h"

#include "client/comp_gl_client.h"
#include "client/comp_gl_eglimage_swapchain.h"

#include <inttypes.h>


static enum u_logging_level ll;

#define EGL_SC_TRACE(...) U_LOG_IFL_T(ll, __VA_ARGS__)
#define EGL_SC_DEBUG(...) U_LOG_IFL_D(ll, __VA_ARGS__)
#define EGL_SC_INFO(...) U_LOG_IFL_I(ll, __VA_ARGS__)
#define EGL_SC_WARN(...) U_LOG_IFL_W(ll, __VA_ARGS__)
#define EGL_SC_ERROR(...) U_LOG_IFL_E(ll, __VA_ARGS__)

DEBUG_GET_ONCE_LOG_OPTION(egl_swapchain_log, "EGL_SWAPCHAIN_LOG", U_LOGGING_WARN)


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
	uint32_t num_images = sc->base.base.base.num_images;
	if (num_images > 0) {
		glDeleteTextures(num_images, &sc->base.base.images[0]);
		U_ZERO_ARRAY(sc->base.base.images);
		for (uint32_t i = 0; i < num_images; ++i) {
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
vk_format_to_srgb(uint64_t format)
{
	switch (format) {
	case 37 /*VK_FORMAT_R8G8B8A8_UNORM*/: return false;
	case 64 /*VK_FORMAT_A2B10G10R10_UNORM_PACK32*/: return false;
	case 50 /*VK_FORMAT_B8G8R8A8_SRGB*/: return true;
	case 124 /*VK_FORMAT_D16_UNORM*/: return false;
	case 44 /*VK_FORMAT_B8G8R8A8_UNORM*/: return false;
	case 129 /*VK_FORMAT_D24_UNORM_S8_UINT*/: return false;
	case 130 /*VK_FORMAT_D32_SFLOAT_S8_UINT*/: return false;
	case 23 /*VK_FORMAT_R8G8B8_UNORM*/: return false;
	case 127 /*VK_FORMAT_S8_UINT*/: return false;
	case 4 /*VK_FORMAT_R5G6B5_UNORM_PACK16*/: return false;
	case 97 /*VK_FORMAT_R16G16B16A16_SFLOAT*/: return false;
	case 126 /*VK_FORMAT_D32_SFLOAT*/: return false;
	case 125 /*VK_FORMAT_X8_D24_UNORM_PACK32*/: return false;
	case 43 /*VK_FORMAT_R8G8B8A8_SRGB*/: return true;
	default: return false;
	}
}
#endif // defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)


struct xrt_swapchain *
client_gl_eglimage_swapchain_create(struct xrt_compositor *xc,
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
		EGL_SC_INFO("Computed row pitch is %" PRIu32 " bytes: %" PRIu32 " bpp, %" PRIu32 " pixels wide",
		            row_pitch, bpp, info->width);
	}
#endif // defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)

	struct xrt_swapchain *native_xsc = &xscn->base;

	struct client_gl_eglimage_swapchain *sc = U_TYPED_CALLOC(struct client_gl_eglimage_swapchain);
	struct xrt_swapchain_gl *xscgl = &sc->base.base;
	struct xrt_swapchain *client_xsc = &xscgl->base;
	client_xsc->destroy = client_gl_eglimage_swapchain_destroy;
	// Fetch the number of images from the native swapchain.
	client_xsc->num_images = native_xsc->num_images;
	sc->base.xscn = xscn;

	sc->display = eglGetCurrentDisplay();

	glGenTextures(native_xsc->num_images, xscgl->images);

	GLuint binding_enum = 0;
	GLuint tex_target = 0;
	ogl_texture_target_for_swapchain_info(info, &tex_target, &binding_enum);
	sc->base.tex_target = tex_target;

	for (uint32_t i = 0; i < native_xsc->num_images; i++) {

		// Bind new texture name to the target.
		glBindTexture(tex_target, xscgl->images[i]);

		EGLClientBuffer native_buffer = NULL;

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
		// see
		// https://android.googlesource.com/platform/cts/+/master/tests/tests/nativehardware/jni/AHardwareBufferGLTest.cpp
		native_buffer = eglGetNativeClientBufferANDROID(xscn->images[i].handle);

		if (NULL == native_buffer) {
			EGL_SC_ERROR("eglGetNativeClientBufferANDROID failed");
			client_gl_eglimage_swapchain_teardown_storage(sc);
			free(sc);
			return NULL;
		}
		EGLint attrs[] = {
		    EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE, EGL_NONE, EGL_NONE,
		};
		if (vk_format_to_srgb(info->format)) {
			attrs[2] = EGL_GL_COLORSPACE_KHR;
			attrs[3] = EGL_GL_COLORSPACE_SRGB_KHR;
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
