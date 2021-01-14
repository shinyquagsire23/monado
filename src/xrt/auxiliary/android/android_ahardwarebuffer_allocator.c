// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief AHardwareBuffer backed image buffer allocator.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

#include "android_ahardwarebuffer_allocator.h"

#include "util/u_misc.h"
#include "util/u_logging.h"
#include "util/u_debug.h"
#include "util/u_handles.h"

#include "xrt/xrt_vulkan_includes.h"

#ifdef XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER
#include <android/hardware_buffer.h>

DEBUG_GET_ONCE_LOG_OPTION(ahardwarebuffer_log, "AHARDWAREBUFFER_LOG", U_LOGGING_WARN)
#define AHB_TRACE(...) U_LOG_IFL_T(debug_get_log_option_ahardwarebuffer_log(), __VA_ARGS__)
#define AHB_DEBUG(...) U_LOG_IFL_D(debug_get_log_option_ahardwarebuffer_log(), __VA_ARGS__)
#define AHB_INFO(...) U_LOG_IFL_I(debug_get_log_option_ahardwarebuffer_log(), __VA_ARGS__)
#define AHB_WARN(...) U_LOG_IFL_W(debug_get_log_option_ahardwarebuffer_log(), __VA_ARGS__)
#define AHB_ERROR(...) U_LOG_IFL_E(debug_get_log_option_ahardwarebuffer_log(), __VA_ARGS__)

static inline enum AHardwareBuffer_Format
vk_format_to_ahardwarebuffer(uint64_t format)
{
	switch (format) {
	case VK_FORMAT_X8_D24_UNORM_PACK32: return AHARDWAREBUFFER_FORMAT_D24_UNORM;
	case VK_FORMAT_D24_UNORM_S8_UINT: return AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT;
	case VK_FORMAT_R5G6B5_UNORM_PACK16: return AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;
	case VK_FORMAT_D16_UNORM: return AHARDWAREBUFFER_FORMAT_D16_UNORM;
	case VK_FORMAT_R8G8B8_UNORM: return AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM;
	case VK_FORMAT_D32_SFLOAT_S8_UINT: return AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
	case VK_FORMAT_S8_UINT: return AHARDWAREBUFFER_FORMAT_S8_UINT;
	case VK_FORMAT_D32_SFLOAT: return AHARDWAREBUFFER_FORMAT_D32_FLOAT;
	case VK_FORMAT_R16G16B16A16_SFLOAT: return AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT;
	case VK_FORMAT_R8G8B8A8_SRGB:
		/* apply EGL_GL_COLORSPACE_KHR, EGL_GL_COLORSPACE_SRGB_KHR! */
		return AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
	case VK_FORMAT_R8G8B8A8_UNORM: return AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
	default: return 0;
	}
}

xrt_result_t
ahardwarebuffer_image_allocate(const struct xrt_swapchain_create_info *xsci, xrt_graphics_buffer_handle_t *out_image)
{
	AHardwareBuffer_Desc desc;
	U_ZERO(&desc);
	enum AHardwareBuffer_Format ahb_format = vk_format_to_ahardwarebuffer(xsci->format);
	if (ahb_format == 0) {
		AHB_ERROR("Could not convert %04" PRIx64 " to AHardwareBuffer_Format!", (uint64_t)xsci->format);
		return XRT_ERROR_ALLOCATION;
	}
	desc.height = xsci->height;
	desc.width = xsci->width;
	desc.format = ahb_format;
	desc.layers = xsci->array_size;
	if (xsci->face_count == 6) {
		desc.usage |= AHARDWAREBUFFER_USAGE_GPU_CUBE_MAP;
		desc.layers *= 6;
	}
	if (0 != (xsci->bits & (XRT_SWAPCHAIN_USAGE_COLOR | XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL))) {
		desc.usage |= AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
	}
	if (0 != (xsci->bits & XRT_SWAPCHAIN_USAGE_SAMPLED)) {
		desc.usage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
	}
	if (0 != (xsci->create & XRT_SWAPCHAIN_CREATE_PROTECTED_CONTENT)) {
		desc.usage |= AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT;
	}

#if __ANDROID_API__ >= 29
	if (0 == AHardwareBuffer_isSupported(&desc)) {
		AHB_ERROR("Computed AHardwareBuffer_Desc is not supported.");
		return XRT_ERROR_ALLOCATION;
	}
#endif

	int ret = AHardwareBuffer_allocate(&desc, out_image);
	if (ret != 0) {
		AHB_ERROR("Failed allocating image.");
		return XRT_ERROR_ALLOCATION;
	}

	return XRT_SUCCESS;
}

static xrt_result_t
ahardwarebuffer_images_allocate(struct xrt_image_native_allocator *xina,
                                const struct xrt_swapchain_create_info *xsci,
                                size_t num_images,
                                struct xrt_image_native *out_images)
{
	AHardwareBuffer_Desc desc;
	U_ZERO(&desc);
	enum AHardwareBuffer_Format ahb_format = vk_format_to_ahardwarebuffer(xsci->format);
	if (ahb_format == 0) {
		AHB_ERROR("Could not convert %04" PRIx64 " to AHardwareBuffer_Format!", (uint64_t)xsci->format);
		return XRT_ERROR_ALLOCATION;
	}
	desc.height = xsci->height;
	desc.width = xsci->width;
	desc.format = ahb_format;
	desc.layers = xsci->array_size;
	if (xsci->face_count == 6) {
		desc.usage |= AHARDWAREBUFFER_USAGE_GPU_CUBE_MAP;
		desc.layers *= 6;
	}
	if (0 != (xsci->bits & (XRT_SWAPCHAIN_USAGE_COLOR | XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL))) {
		desc.usage |= AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
	}
	if (0 != (xsci->bits & XRT_SWAPCHAIN_USAGE_SAMPLED)) {
		desc.usage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
	}
	if (0 != (xsci->create & XRT_SWAPCHAIN_CREATE_PROTECTED_CONTENT)) {
		desc.usage |= AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT;
	}

#if __ANDROID_API__ >= 29
	if (0 == AHardwareBuffer_isSupported(&desc)) {
		AHB_ERROR("Computed AHardwareBuffer_Desc is not supported.");
		return XRT_ERROR_ALLOCATION;
	}
#endif

	memset(out_images, 0, sizeof(*out_images) * num_images);
	bool failed = false;
	for (size_t i = 0; i < num_images; ++i) {
		int ret = AHardwareBuffer_allocate(&desc, &(out_images[i].handle));
		if (ret != 0) {
			AHB_ERROR("Failed allocating image %d.", (int)i);
			failed = true;
			break;
		}
	}
	if (failed) {
		for (size_t i = 0; i < num_images; ++i) {
			u_graphics_buffer_unref(&(out_images[i].handle));
		}
		return XRT_ERROR_ALLOCATION;
	}
	return XRT_SUCCESS;
}

static xrt_result_t
ahardwarebuffer_images_free(struct xrt_image_native_allocator *xina, size_t num_images, struct xrt_image_native *images)
{
	for (size_t i = 0; i < num_images; ++i) {
		u_graphics_buffer_unref(&(images[i].handle));
	}
	return XRT_SUCCESS;
}
static void
ahardwarebuffer_destroy(struct xrt_image_native_allocator *xina)
{
	if (xina != NULL) {
		free(xina);
	}
}

struct xrt_image_native_allocator *
android_ahardwarebuffer_allocator_create()
{
	struct xrt_image_native_allocator *xina = U_TYPED_CALLOC(struct xrt_image_native_allocator);
	xina->images_allocate = ahardwarebuffer_images_allocate;
	xina->images_free = ahardwarebuffer_images_free;
	xina->destroy = ahardwarebuffer_destroy;
	return xina;
}

#endif // XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER
