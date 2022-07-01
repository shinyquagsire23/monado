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

VkExternalMemoryHandleTypeFlags
get_image_memory_handle_type(void)
{
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	return VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)
	return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
	return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;
#else
#error "need port"
#endif
}

static VkResult
create_image(struct vk_bundle *vk, const struct xrt_swapchain_create_info *info, struct vk_image *out_image)
{
	// This is the format we allocate the image in, can be changed further down.
	VkFormat image_format = (VkFormat)info->format;
	VkImageCreateFlags image_create_flags = 0;

	VkImageUsageFlags image_usage = vk_csci_get_image_usage_flags( //
	    vk,                                                        //
	    image_format,                                              //
	    info->bits);                                               //
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

	xrt_result_t xret = ahardwarebuffer_image_allocate(info, &a_buffer);
	if (xret != XRT_SUCCESS) {
		U_LOG_E("Failed to ahardwarebuffer_image_allocate.");
		// ahardwarebuffer_image_allocate only returns XRT_ERROR_ALLOCATION
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}

	// Out->pNext
	VkAndroidHardwareBufferFormatPropertiesANDROID a_buffer_format_props = {
	    .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
	};

	// Out
	VkAndroidHardwareBufferPropertiesANDROID a_buffer_props = {
	    .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
	    .pNext = &a_buffer_format_props,
	};

	ret = vk->vkGetAndroidHardwareBufferPropertiesANDROID(vk->device, a_buffer, &a_buffer_props);
	if (ret != VK_SUCCESS) {
		U_LOG_E("vkGetAndroidHardwareBufferPropertiesANDROID: %s", vk_result_string(ret));
		return ret;
	}

	//! @todo Actually use this buffer for something other then getting the format.
	// Does null-check, validity check and clears.
	u_graphics_buffer_unref(&a_buffer);
#endif

	/*
	 *
	 * Start of create image call.
	 *
	 */
	void *next_chain = NULL;
#define CHAIN(STRUCT)                                                                                                  \
	do {                                                                                                           \
		(STRUCT).pNext = next_chain;                                                                           \
		next_chain = (void *)&(STRUCT);                                                                        \
	} while (false)

	/*
	 * Create the image.
	 */

	VkExternalMemoryHandleTypeFlags memory_handle_type = get_image_memory_handle_type();

	VkExternalMemoryImageCreateInfoKHR external_memory_image_create_info = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR,
	    .handleTypes = memory_handle_type,
	    .pNext = next_chain,
	};
	CHAIN(external_memory_image_create_info);

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	VkExternalFormatANDROID format_android = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
	    .externalFormat = a_buffer_format_props.externalFormat,
	};
	CHAIN(format_android);

#ifdef VK_KHR_image_format_list
	VkImageFormatListCreateInfoKHR image_format_list_create_info = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR,
	    .pNext = NULL,
	    .viewFormatCount = 2,
	    .pViewFormats =
	        (VkFormat[2]){
	            VK_FORMAT_R8G8B8A8_UNORM,
	            VK_FORMAT_R8G8B8A8_SRGB,
	        },
	};
#endif
	// Android can't allocate native sRGB.
	// Use UNORM and correct gamma later.
	if (image_format == VK_FORMAT_R8G8B8A8_SRGB) {
		image_format = VK_FORMAT_R8G8B8A8_UNORM;

		// https://www.khronos.org/registry/vulkan/specs/1.0/html/vkspec.html#VUID-VkImageViewCreateInfo-image-01019
		image_create_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

		// https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VkImageCreateInfo.html#VUID-VkImageCreateInfo-pNext-02396
		format_android.externalFormat = 0;
		assert(a_buffer_format_props.format != VK_FORMAT_UNDEFINED); // Make sure there is a Vulkan format.
		assert(format_android.externalFormat == 0);

#ifdef VK_KHR_image_format_list
		if (vk->has_KHR_image_format_list) {
			CHAIN(image_format_list_create_info);
		}
#endif
	}
#endif

	if (info->face_count == 6) {
		image_create_flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	}

	VkImageCreateInfo create_info = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .pNext = next_chain,
	    .flags = image_create_flags,
	    .imageType = VK_IMAGE_TYPE_2D,
	    .format = image_format,
	    .extent = {.width = info->width, .height = info->height, .depth = 1},
	    .mipLevels = info->mip_count,
	    .arrayLayers = info->array_size * info->face_count,
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

	// In
	VkImageMemoryRequirementsInfo2 memory_requirements_info = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
	    .image = image,
	};

	// Out->pNext
	VkMemoryDedicatedRequirements memory_dedicated_requirements = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
	};

	// Out
	VkMemoryRequirements2 memory_requirements = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
	    .pNext = &memory_dedicated_requirements,
	};

	vk->vkGetImageMemoryRequirements2(vk->device, &memory_requirements_info, &memory_requirements);

	/*
	 * On tegra we must not use dedicated allocation when it is only preferred to avoid black textures and driver
	 * errors when blitting from opengl interop textures.
	 *
	 * On desktop nvidia and everywhere else we must always use dedicated allocation even when it is neither
	 * preferred nor required to avoid fences timing out and driver errors like "Graphics Exception on GPC 0: 3D-C
	 * MEMLAYOUT Violation."
	 */
	VkBool32 use_dedicated_allocation;
	if (vk->is_tegra) {
		use_dedicated_allocation = memory_dedicated_requirements.requiresDedicatedAllocation != VK_FALSE;
	} else {
		use_dedicated_allocation = VK_TRUE;
	}

	U_LOG_D("create_image: Use dedicated allocation: %d (preferred: %d, required: %d)", use_dedicated_allocation,
	        memory_dedicated_requirements.prefersDedicatedAllocation,
	        memory_dedicated_requirements.requiresDedicatedAllocation);

	/*
	 * Create and bind the memory.
	 */

	// In->pNext->pNext
	VkMemoryDedicatedAllocateInfoKHR dedicated_memory_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR,
	    .image = image,
	    .buffer = VK_NULL_HANDLE,
	};

	// In->pNext
	VkExportMemoryAllocateInfo export_alloc_info = {
	    .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR,
	    .pNext = use_dedicated_allocation ? &dedicated_memory_info : NULL,
	    .handleTypes = memory_handle_type,
	};

	ret = vk_alloc_and_bind_image_memory(   //
	    vk,                                 // vk_bundle
	    image,                              // image
	    SIZE_MAX,                           // max_size
	    &export_alloc_info,                 // pNext_for_allocate
	    "vk_image_allocator::create_image", // caller_name
	    &device_memory,                     // out_mem
	    &size);                             // out_size
	if (ret != VK_SUCCESS) {
		vk->vkDestroyImage(vk->device, image, NULL);
		return ret;
	}

	out_image->handle = image;
	out_image->memory = device_memory;
	out_image->size = size;
	out_image->use_dedicated_allocation = use_dedicated_allocation;

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
               uint32_t image_count,
               struct vk_image_collection *out_vkic)
{
	VkResult ret = VK_SUCCESS;

	if (image_count > ARRAY_SIZE(out_vkic->images)) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}


	size_t i = 0;
	for (; i < image_count; i++) {
		ret = create_image(vk, xscci, &out_vkic->images[i]);
		if (ret != VK_SUCCESS) {
			break;
		}
	}

	// Set the fields.
	out_vkic->image_count = image_count;
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
                   uint32_t image_count,
                   struct vk_image_collection *out_vkic)
{
	VkResult ret = VK_ERROR_INITIALIZATION_FAILED;

	if (image_count > ARRAY_SIZE(out_vkic->images)) {
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}


	size_t i = 0;
	for (; i < image_count; i++) {
		// Ensure that all handles are consumed or none are.
		xrt_graphics_buffer_handle_t buf = u_graphics_buffer_ref(native_images[i].handle);

		if (!xrt_graphics_buffer_is_valid(buf)) {
			U_LOG_E("Could not ref/duplicate graphics buffer handle");
			ret = VK_ERROR_INITIALIZATION_FAILED;
			break;
		}

		ret = vk_create_image_from_native( //
		    vk,                            // vk_bundle
		    xscci,                         // info
		    &native_images[i],             // image_native
		    &out_vkic->images[i].handle,   // out_image
		    &out_vkic->images[i].memory);  // out_mem
		if (ret != VK_SUCCESS) {
			u_graphics_buffer_unref(&buf);
			break;
		}
		native_images[i].handle = buf;
	}
	// Set the fields.
	out_vkic->image_count = image_count;
	out_vkic->info = *xscci;

	if (ret == VK_SUCCESS) {
		// We have consumed all handles now, close all of the copies we
		// made, all this to make sure we do all or nothing.
		for (size_t k = 0; k < image_count; k++) {
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
	for (size_t i = 0; i < vkic->image_count; i++) {
		destroy_image(vk, &vkic->images[i]);
	}
	vkic->image_count = 0;
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
	for (; i < vkic->image_count && i < max_handles; i++) {
		ret = vk_get_native_handle_from_device_memory(vk, vkic->images[i].memory, &out_handles[i]);
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
