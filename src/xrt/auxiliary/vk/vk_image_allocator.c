// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan image allocator helper.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_vk
 */

#include "util/u_misc.h"
#include "util/u_logging.h"
#include "util/u_handles.h"

#include "vk/vk_image_allocator.h"

#include <xrt/xrt_handles.h>

#ifdef XRT_OS_LINUX
#include <unistd.h>
#endif

#ifdef XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER
#include "android/android_ahardwarebuffer_allocator.h"
#endif
/*
 *
 * Helper functions.
 *
 */

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)

static VkResult
get_device_memory_handle(struct vk_bundle *vk, VkDeviceMemory device_memory, xrt_graphics_buffer_handle_t *out_handle)
{
	// vkGetMemoryFdKHR parameter
	VkMemoryGetFdInfoKHR fd_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
	    .memory = device_memory,
	    .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
	};

	int fd;
	VkResult ret = vk->vkGetMemoryFdKHR(vk->device, &fd_info, &fd);
	if (ret != VK_SUCCESS) {
		// COMP_ERROR(c, "->image - vkGetMemoryFdKHR: %s",
		//           vk_result_string(ret));
		return ret;
	}

	*out_handle = fd;

	return ret;
}

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)

static VkResult
get_device_memory_handle(struct vk_bundle *vk, VkDeviceMemory device_memory, xrt_graphics_buffer_handle_t *out_handle)
{
	// vkGetMemoryAndroidHardwareBufferANDROID parameter
	VkMemoryGetAndroidHardwareBufferInfoANDROID ahb_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
	    .pNext = NULL,
	    .memory = device_memory,
	};

	AHardwareBuffer *buf = NULL;
	VkResult ret = vk->vkGetMemoryAndroidHardwareBufferANDROID(vk->device, &ahb_info, &buf);
	if (ret != VK_SUCCESS) {
		return ret;
	}

	*out_handle = buf;

	return ret;
}

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)

static VkResult
get_device_memory_handle(struct vk_bundle *vk, VkDeviceMemory device_memory, xrt_graphics_buffer_handle_t *out_handle)
{
	// vkGetMemoryWin32HandleKHR parameter
	VkMemoryGetWin32HandleInfoKHR win32_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
	    .pNext = NULL,
	    .memory = device_memory,
	    .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR,
	};

	HANDLE handle = NULL;
	VkResult ret = vk->vkGetMemoryWin32HandleKHR(vk->device, &win32_info, &handle);
	if (ret != VK_SUCCESS) {
		return ret;
	}

	*out_handle = handle;

	return ret;
}
#else
#error "Needs port"
#endif

static VkResult
create_image(struct vk_bundle *vk, const struct xrt_swapchain_create_info *info, struct vk_image *out_image)
{
	VkImageUsageFlags image_usage = vk_swapchain_usage_flags(vk, (VkFormat)info->format, info->bits);
	if (image_usage == 0) {
		U_LOG_E("create_image: Unsupported swapchain usage flags");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkDeviceMemory device_memory = VK_NULL_HANDLE;
	VkImage image = VK_NULL_HANDLE;
	VkResult ret = VK_SUCCESS;
	VkDeviceSize size;

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	/*
	 * Get AHardwareBuffer props
	 */

	AHardwareBuffer *a_buffer = NULL;

	xrt_result_t xrt_res = ahardwarebuffer_image_allocate(info, &a_buffer);
	if (xrt_res != XRT_SUCCESS) {
		U_LOG_E("Failed to ahardwarebuffer_image_allocate.");
		return ret;
	}

	VkAndroidHardwareBufferFormatPropertiesANDROID a_buffer_format_props = {
	    .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
	    .format = (VkFormat)info->format,
	};

	VkAndroidHardwareBufferPropertiesANDROID a_buffer_props = {
	    .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
	    .pNext = &a_buffer_format_props,
	};

	ret = vk->vkGetAndroidHardwareBufferPropertiesANDROID(vk->device, a_buffer, &a_buffer_props);
	if (ret != VK_SUCCESS) {
		U_LOG_E("vkGetAndroidHardwareBufferPropertiesANDROID: %s", vk_result_string(ret));
		return ret;
	}
#endif

	/*
	 * Create the image.
	 */

	VkExternalMemoryImageCreateInfoKHR external_memory_image_create_info = {
		.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR,
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
#else
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
#endif
	};

	VkFormat format = info->format;
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	VkExternalFormatANDROID format_android = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
	    .pNext = &external_memory_image_create_info,
	    .externalFormat = a_buffer_format_props.externalFormat,
	};

	// Android can't allocate native sRGB.
	// Use UNORM and correct gamma later.
	if (format == VK_FORMAT_R8G8B8A8_SRGB) {
		format = VK_FORMAT_R8G8B8A8_UNORM;
	}
#endif

	VkImageCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
		.pNext = &format_android,
#else
		.pNext = &external_memory_image_create_info,
#endif
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = {.width = info->width, .height = info->height, .depth = 1},
		.mipLevels = info->mip_count,
		.arrayLayers = info->array_size,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = image_usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	ret = vk->vkCreateImage(vk->device, &create_info, NULL, &image);
	if (ret != VK_SUCCESS) {
		U_LOG_E("vkCreateImage: %s", vk_result_string(ret));
		return ret;
	}

	/*
	 * Create and bind the memory.
	 */
	// vkAllocateMemory parameters
	VkMemoryDedicatedAllocateInfoKHR dedicated_memory_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR,
	    .image = image,
	    .buffer = VK_NULL_HANDLE,
	};

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)

	VkExportMemoryAllocateInfo export_alloc_info = {
	    .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR,
	    .pNext = &dedicated_memory_info,
	    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
	};

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)

	VkExportMemoryAllocateInfo export_alloc_info = {
	    .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR,
	    .pNext = &dedicated_memory_info,
	    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
	};

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)

	VkExportMemoryAllocateInfo export_alloc_info = {
	    .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR,
	    .pNext = &dedicated_memory_info,
	    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
	};

#else
#error "need port"
#endif

	ret = vk_alloc_and_bind_image_memory(vk, image, SIZE_MAX, &export_alloc_info, &device_memory, &size);
	if (ret != VK_SUCCESS) {
		U_LOG_E("vkAllocateMemory: %s", vk_result_string(ret));
		vk->vkDestroyImage(vk->device, image, NULL);
		return ret;
	}

	out_image->handle = image;
	out_image->memory = device_memory;
	out_image->size = size;

	return ret;
}

static void
destroy_image(struct vk_bundle *vk, struct vk_image *image)
{
	if (image->handle != VK_NULL_HANDLE) {
		vk->vkDestroyImage(vk->device, image->handle, NULL);
		image->handle = VK_NULL_HANDLE;
	}
	if (image->memory != VK_NULL_HANDLE) {
		vk->vkFreeMemory(vk->device, image->memory, NULL);
		image->memory = VK_NULL_HANDLE;
	}
}


/*
 *
 * 'Exported' functions.
 *
 */

VkResult
vk_ic_allocate(struct vk_bundle *vk,
               const struct xrt_swapchain_create_info *xscci,
               uint32_t num_images,
               struct vk_image_collection *out_vkic)
{
	VkResult ret = VK_SUCCESS;

	if (num_images > ARRAY_SIZE(out_vkic->images)) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}


	size_t i = 0;
	for (; i < num_images; i++) {
		ret = create_image(vk, xscci, &out_vkic->images[i]);
		if (ret != VK_SUCCESS) {
			break;
		}
	}

	// Set the fields.
	out_vkic->num_images = num_images;
	out_vkic->info = *xscci;

	if (ret == VK_SUCCESS) {
		return ret;
	}

	// i is the index of the failed image, everything before that index
	// succeeded and needs to be destroyed. If i is zero no call succeeded.
	while (i > 0) {
		i--;
		destroy_image(vk, &out_vkic->images[i]);
	}

	U_ZERO(out_vkic);

	return ret;
}

/*!
 * Imports and set images from the given FDs.
 */
VkResult
vk_ic_from_natives(struct vk_bundle *vk,
                   const struct xrt_swapchain_create_info *xscci,
                   struct xrt_image_native *native_images,
                   uint32_t num_images,
                   struct vk_image_collection *out_vkic)
{
	VkResult ret = VK_ERROR_INITIALIZATION_FAILED;

	if (num_images > ARRAY_SIZE(out_vkic->images)) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}


	size_t i = 0;
	for (; i < num_images; i++) {
		// Ensure that all handles are consumed or none are.
		xrt_graphics_buffer_handle_t buf = u_graphics_buffer_ref(native_images[i].handle);

		ret = vk_create_image_from_native(vk, xscci, &native_images[i], &out_vkic->images[i].handle,
		                                  &out_vkic->images[i].memory);
		if (ret != VK_SUCCESS) {
			u_graphics_buffer_unref(&buf);
			break;
		}
		native_images[i].handle = buf;
	}
	// Set the fields.
	out_vkic->num_images = num_images;
	out_vkic->info = *xscci;

	if (ret == VK_SUCCESS) {
		// We have consumed all handles now, close all of the copies we
		// made, all this to make sure we do all or nothing.
		for (size_t k = 0; k < num_images; k++) {
			u_graphics_buffer_unref(&native_images[k].handle);
			native_images[k].size = 0;
		}
		return ret;
	}

	// i is the index of the failed image, everything before that index
	// succeeded and needs to be destroyed. If i is zero no call succeeded.
	while (i > 0) {
		i--;
		destroy_image(vk, &out_vkic->images[i]);
	}

	U_ZERO(out_vkic);

	return ret;
}

void
vk_ic_destroy(struct vk_bundle *vk, struct vk_image_collection *vkic)
{
	for (size_t i = 0; i < vkic->num_images; i++) {
		destroy_image(vk, &vkic->images[i]);
	}
	vkic->num_images = 0;
	U_ZERO(&vkic->info);
}

VkResult
vk_ic_get_handles(struct vk_bundle *vk,
                  struct vk_image_collection *vkic,
                  uint32_t max_handles,
                  xrt_graphics_buffer_handle_t *out_handles)
{
	VkResult ret = VK_SUCCESS;

	size_t i = 0;
	for (; i < vkic->num_images && i < max_handles; i++) {
		ret = get_device_memory_handle(vk, vkic->images[i].memory, &out_handles[i]);
		if (ret != VK_SUCCESS) {
			break;
		}
	}

	if (ret == VK_SUCCESS) {
		return ret;
	}

	// i is the index of the failed fd, everything before that index
	// succeeded and needs to be closed. If i is zero no call succeeded.
	while (i > 0) {
		i--;
		u_graphics_buffer_unref(&out_handles[i]);
	}

	return ret;
}
