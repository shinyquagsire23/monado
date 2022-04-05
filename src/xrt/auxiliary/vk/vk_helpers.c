// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common Vulkan code.
 *
 * Note that some sections of this are generated
 * by `scripts/generate_vk_helpers.py` - lists of functions
 * and of optional extensions to check for. In those,
 * please update the script and run it, instead of editing
 * directly in this file. The generated parts are delimited
 * by special comments.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_vk
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "vk/vk_helpers.h"

#include <xrt/xrt_handles.h>


/*
 *
 * String helper functions.
 *
 */

#define ENUM_TO_STR(r)                                                                                                 \
	case r: return #r

const char *
vk_result_string(VkResult code)
{
	switch (code) {
		ENUM_TO_STR(VK_SUCCESS);
		ENUM_TO_STR(VK_NOT_READY);
		ENUM_TO_STR(VK_TIMEOUT);
		ENUM_TO_STR(VK_EVENT_SET);
		ENUM_TO_STR(VK_EVENT_RESET);
		ENUM_TO_STR(VK_INCOMPLETE);
		ENUM_TO_STR(VK_ERROR_OUT_OF_HOST_MEMORY);
		ENUM_TO_STR(VK_ERROR_OUT_OF_DEVICE_MEMORY);
		ENUM_TO_STR(VK_ERROR_INITIALIZATION_FAILED);
		ENUM_TO_STR(VK_ERROR_DEVICE_LOST);
		ENUM_TO_STR(VK_ERROR_MEMORY_MAP_FAILED);
		ENUM_TO_STR(VK_ERROR_LAYER_NOT_PRESENT);
		ENUM_TO_STR(VK_ERROR_EXTENSION_NOT_PRESENT);
		ENUM_TO_STR(VK_ERROR_FEATURE_NOT_PRESENT);
		ENUM_TO_STR(VK_ERROR_INCOMPATIBLE_DRIVER);
		ENUM_TO_STR(VK_ERROR_TOO_MANY_OBJECTS);
		ENUM_TO_STR(VK_ERROR_FORMAT_NOT_SUPPORTED);
		ENUM_TO_STR(VK_ERROR_SURFACE_LOST_KHR);
		ENUM_TO_STR(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
		ENUM_TO_STR(VK_SUBOPTIMAL_KHR);
		ENUM_TO_STR(VK_ERROR_OUT_OF_DATE_KHR);
		ENUM_TO_STR(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
		ENUM_TO_STR(VK_ERROR_VALIDATION_FAILED_EXT);
		ENUM_TO_STR(VK_ERROR_INVALID_SHADER_NV);
		ENUM_TO_STR(VK_ERROR_INVALID_EXTERNAL_HANDLE);
		ENUM_TO_STR(VK_ERROR_NOT_PERMITTED_EXT);
	default: return "UNKNOWN RESULT";
	}
}

const char *
vk_format_string(VkFormat code)
{
	switch (code) {
		ENUM_TO_STR(VK_FORMAT_A2B10G10R10_UNORM_PACK32);
		ENUM_TO_STR(VK_FORMAT_R16G16B16A16_UNORM);
		ENUM_TO_STR(VK_FORMAT_R16G16B16A16_SFLOAT);
		ENUM_TO_STR(VK_FORMAT_R16G16B16_UNORM);
		ENUM_TO_STR(VK_FORMAT_R16G16B16_SFLOAT);
		ENUM_TO_STR(VK_FORMAT_R5G6B5_UNORM_PACK16);
		ENUM_TO_STR(VK_FORMAT_B5G6R5_UNORM_PACK16);
		ENUM_TO_STR(VK_FORMAT_R8G8B8A8_SRGB);
		ENUM_TO_STR(VK_FORMAT_B8G8R8A8_SRGB);
		ENUM_TO_STR(VK_FORMAT_R8G8B8_SRGB);
		ENUM_TO_STR(VK_FORMAT_B8G8R8_SRGB);
		ENUM_TO_STR(VK_FORMAT_R8G8B8A8_UNORM);
		ENUM_TO_STR(VK_FORMAT_B8G8R8A8_UNORM);
		ENUM_TO_STR(VK_FORMAT_R8G8B8_UNORM);
		ENUM_TO_STR(VK_FORMAT_B8G8R8_UNORM);
		ENUM_TO_STR(VK_FORMAT_D16_UNORM);
		ENUM_TO_STR(VK_FORMAT_D16_UNORM_S8_UINT);
		ENUM_TO_STR(VK_FORMAT_D24_UNORM_S8_UINT);
		ENUM_TO_STR(VK_FORMAT_X8_D24_UNORM_PACK32);
		ENUM_TO_STR(VK_FORMAT_D32_SFLOAT);
		ENUM_TO_STR(VK_FORMAT_D32_SFLOAT_S8_UINT);
		ENUM_TO_STR(VK_FORMAT_S8_UINT);
		ENUM_TO_STR(VK_FORMAT_UNDEFINED);
	default: return "UNKNOWN FORMAT";
	}
}

const char *
vk_format_feature_string(VkFormatFeatureFlagBits code)
{
	switch (code) {
		ENUM_TO_STR(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
		ENUM_TO_STR(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
		ENUM_TO_STR(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
		ENUM_TO_STR(VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
		ENUM_TO_STR(VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
		ENUM_TO_STR(VK_FORMAT_R5G6B5_UNORM_PACK16);
	default: return "UNKNOWN FORMAT FEATURE";
	}
}

const char *
xrt_swapchain_usage_string(enum xrt_swapchain_usage_bits code)
{
	switch (code) {
		ENUM_TO_STR(XRT_SWAPCHAIN_USAGE_COLOR);
		ENUM_TO_STR(XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL);
		ENUM_TO_STR(XRT_SWAPCHAIN_USAGE_UNORDERED_ACCESS);
		ENUM_TO_STR(XRT_SWAPCHAIN_USAGE_TRANSFER_SRC);
		ENUM_TO_STR(XRT_SWAPCHAIN_USAGE_TRANSFER_DST);
		ENUM_TO_STR(XRT_SWAPCHAIN_USAGE_SAMPLED);
		ENUM_TO_STR(XRT_SWAPCHAIN_USAGE_MUTABLE_FORMAT);
		ENUM_TO_STR(XRT_SWAPCHAIN_USAGE_INPUT_ATTACHMENT);
	default: return "UNKNOWN SWAPCHAIN USAGE";
	}
}

const char *
vk_present_mode_string(VkPresentModeKHR code)
{
	switch (code) {
		ENUM_TO_STR(VK_PRESENT_MODE_FIFO_KHR);
		ENUM_TO_STR(VK_PRESENT_MODE_MAILBOX_KHR);
		ENUM_TO_STR(VK_PRESENT_MODE_IMMEDIATE_KHR);
		ENUM_TO_STR(VK_PRESENT_MODE_FIFO_RELAXED_KHR);
		ENUM_TO_STR(VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR);
		ENUM_TO_STR(VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR);
	default: return "UNKNOWN MODE";
	}
}

const char *
vk_power_state_string(VkDisplayPowerStateEXT code)
{
	switch (code) {
		ENUM_TO_STR(VK_DISPLAY_POWER_STATE_OFF_EXT);
		ENUM_TO_STR(VK_DISPLAY_POWER_STATE_SUSPEND_EXT);
		ENUM_TO_STR(VK_DISPLAY_POWER_STATE_ON_EXT);
	default: return "UNKNOWN MODE";
	}
}

const char *
vk_color_space_string(VkColorSpaceKHR code)
{
	switch (code) {
		ENUM_TO_STR(VK_COLORSPACE_SRGB_NONLINEAR_KHR);
	default: return "UNKNOWN COLOR SPACE";
	}
}

bool
vk_has_error(VkResult res, const char *fun, const char *file, int line)
{
	if (res != VK_SUCCESS) {
		U_LOG_E("%s failed with %s in %s:%d", fun, vk_result_string(res), file, line);
		return true;
	}
	return false;
}


/*
 *
 * Functions.
 *
 */

bool
vk_get_memory_type(struct vk_bundle *vk, uint32_t type_bits, VkMemoryPropertyFlags memory_props, uint32_t *out_type_id)
{

	uint32_t i_supported = type_bits;
	for (uint32_t i = 0; i < vk->device_memory_props.memoryTypeCount; i++) {
		uint32_t propertyFlags = vk->device_memory_props.memoryTypes[i].propertyFlags;
		if ((i_supported & 1) == 1) {
			if ((propertyFlags & memory_props) == memory_props) {
				*out_type_id = i;
				return true;
			}
		}
		i_supported >>= 1;
	}

	VK_DEBUG(vk, "Could not find memory type!");

	VK_TRACE(vk, "Requested flags: %d (type bits %d with %d memory types)", memory_props, type_bits,
	         vk->device_memory_props.memoryTypeCount);

	i_supported = type_bits;
	VK_TRACE(vk, "Supported flags:");
	for (uint32_t i = 0; i < vk->device_memory_props.memoryTypeCount; i++) {
		uint32_t propertyFlags = vk->device_memory_props.memoryTypes[i].propertyFlags;
		if ((i_supported & 1) == 1) {
			VK_TRACE(vk, "    %d", propertyFlags);
		}
		i_supported >>= 1;
	}

	return false;
}

VkResult
vk_alloc_and_bind_image_memory(struct vk_bundle *vk,
                               VkImage image,
                               size_t max_size,
                               const void *pNext_for_allocate,
                               VkDeviceMemory *out_mem,
                               VkDeviceSize *out_size)
{
	VkMemoryRequirements memory_requirements;
	vk->vkGetImageMemoryRequirements(vk->device, image, &memory_requirements);

	if (max_size > 0 && memory_requirements.size > max_size) {
		VK_ERROR(vk, "client_vk_swapchain - Got too little memory %u vs %u\n",
		         (uint32_t)memory_requirements.size, (uint32_t)max_size);
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}
	if (out_size != NULL) {
		*out_size = memory_requirements.size;
	}

	uint32_t memory_type_index = UINT32_MAX;
	if (!vk_get_memory_type(vk, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	                        &memory_type_index)) {
		VK_ERROR(vk, "vk_get_memory_type failed!");
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}

	VkMemoryAllocateInfo alloc_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	    .pNext = pNext_for_allocate,
	    .allocationSize = memory_requirements.size,
	    .memoryTypeIndex = memory_type_index,
	};

	VkDeviceMemory device_memory = VK_NULL_HANDLE;
	VkResult ret = vk->vkAllocateMemory(vk->device, &alloc_info, NULL, &device_memory);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkAllocateMemory: %s", vk_result_string(ret));
		return ret;
	}

	// Bind the memory to the image.
	ret = vk->vkBindImageMemory(vk->device, image, device_memory, 0);
	if (ret != VK_SUCCESS) {
		// Clean up memory
		vk->vkFreeMemory(vk->device, device_memory, NULL);
		VK_ERROR(vk, "vkBindImageMemory: %s", vk_result_string(ret));
		return ret;
	}

	*out_mem = device_memory;
	return ret;
}

VkResult
vk_create_image_simple(struct vk_bundle *vk,
                       VkExtent2D extent,
                       VkFormat format,
                       VkImageUsageFlags usage,
                       VkDeviceMemory *out_mem,
                       VkImage *out_image)
{
	VkImageCreateInfo image_info = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .imageType = VK_IMAGE_TYPE_2D,
	    .format = format,
	    .extent =
	        {
	            .width = extent.width,
	            .height = extent.height,
	            .depth = 1,
	        },
	    .mipLevels = 1,
	    .arrayLayers = 1,
	    .samples = VK_SAMPLE_COUNT_1_BIT,
	    .tiling = VK_IMAGE_TILING_OPTIMAL,
	    .usage = usage,
	    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	    .queueFamilyIndexCount = 0,
	    .pQueueFamilyIndices = NULL,
	    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VkImage image;
	VkResult ret = vk->vkCreateImage(vk->device, &image_info, NULL, &image);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateImage: %s", vk_result_string(ret));
		// Nothing to cleanup
		return ret;
	}

	ret = vk_alloc_and_bind_image_memory(vk, image, SIZE_MAX, NULL, out_mem, NULL);
	if (ret != VK_SUCCESS) {
		// Clean up image
		vk->vkDestroyImage(vk->device, image, NULL);
		return ret;
	}

	*out_image = image;
	return ret;
}

VkResult
vk_create_image_advanced(struct vk_bundle *vk,
                         VkExtent3D extent,
                         VkFormat format,
                         VkImageTiling image_tiling,
                         VkImageUsageFlags image_usage_flags,
                         VkMemoryPropertyFlags memory_property_flags,
                         VkDeviceMemory *out_mem,
                         VkImage *out_image)
{
	VkResult ret = VK_SUCCESS;
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory device_memory = VK_NULL_HANDLE;

	VkImageCreateInfo image_info = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .imageType = VK_IMAGE_TYPE_2D,
	    .format = format,
	    .extent = extent,
	    .mipLevels = 1,
	    .arrayLayers = 1,
	    .samples = VK_SAMPLE_COUNT_1_BIT,
	    .tiling = image_tiling,
	    .usage = image_usage_flags,
	    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	    .queueFamilyIndexCount = 0,
	    .pQueueFamilyIndices = NULL,
	    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};


	ret = vk->vkCreateImage(vk->device, &image_info, NULL, &image);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateImage: %s", vk_result_string(ret));
		// Nothing to cleanup
		return ret;
	}

	VkMemoryRequirements memory_requirements;
	vk->vkGetImageMemoryRequirements( //
	    vk->device,                   // device
	    image,                        // image
	    &memory_requirements);        // pMemoryRequirements

	VkMemoryAllocateInfo memory_allocate_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	    .allocationSize = memory_requirements.size,
	};

	if (!vk_get_memory_type(                          //
	        vk,                                       //
	        memory_requirements.memoryTypeBits,       //
	        memory_property_flags,                    //
	        &memory_allocate_info.memoryTypeIndex)) { //
		VK_ERROR(vk, "vk_get_memory_type failed: 'false'\n\tFailed to find a matching memory type.");
		ret = VK_ERROR_OUT_OF_DEVICE_MEMORY;
		goto err_image;
	}

	ret = vk->vkAllocateMemory( //
	    vk->device,             // device
	    &memory_allocate_info,  // pAllocateInfo
	    NULL,                   // pAllocator
	    &device_memory);        // pMemory
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkAllocateMemory: %s", vk_result_string(ret));
		goto err_image;
	}

	ret = vk->vkBindImageMemory( //
	    vk->device,              // device
	    image,                   // image
	    device_memory,           // memory
	    0);                      // memoryOffset
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkBindImageMemory: %s", vk_result_string(ret));
		goto err_memory;
	}

	*out_image = image;
	*out_mem = device_memory;

	return ret;

err_memory:
	vk->vkFreeMemory(vk->device, device_memory, NULL);
err_image:
	vk->vkDestroyImage(vk->device, image, NULL);

	return ret;
}

VkResult
vk_create_image_from_native(struct vk_bundle *vk,
                            const struct xrt_swapchain_create_info *info,
                            struct xrt_image_native *image_native,
                            VkImage *out_image,
                            VkDeviceMemory *out_mem)
{
	VkResult ret = VK_SUCCESS;

	// This is the format we allocate the image in, can be changed further down.
	VkFormat image_format = (VkFormat)info->format;
	VkImageUsageFlags image_usage = vk_csci_get_image_usage_flags( //
	    vk,                                                        //
	    image_format,                                              //
	    info->bits);                                               //
	if (image_usage == 0) {
		U_LOG_E("vk_create_image_from_native: Unsupported swapchain usage flags");
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}

	VkExternalMemoryHandleTypeFlags handle_type = vk_csci_get_image_external_handle_type(vk);

	// In->pNext
	VkPhysicalDeviceExternalImageFormatInfo external_image_format_info = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
	    .handleType = handle_type,
	};

	// In
	VkPhysicalDeviceImageFormatInfo2 format_info = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
	    .pNext = &external_image_format_info,
	    .format = image_format,
	    .type = VK_IMAGE_TYPE_2D,
	    .tiling = VK_IMAGE_TILING_OPTIMAL,
	    .usage = image_usage,
	};

	// Out->pNext
	VkExternalImageFormatProperties external_format_properties = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
	};

	// Out
	VkImageFormatProperties2 format_properties = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
	    .pNext = &external_format_properties,
	};

	ret = vk->vkGetPhysicalDeviceImageFormatProperties2(vk->physical_device, &format_info, &format_properties);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkGetPhysicalDeviceImageFormatProperties2: %s", vk_result_string(ret));
		// Nothing to cleanup
		return ret;
	}

	VkExternalMemoryFeatureFlags features =
	    external_format_properties.externalMemoryProperties.externalMemoryFeatures;

	if ((features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) == 0) {
		VK_ERROR(vk, "External memory handle is not importable (has features: %d)", features);
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	// In->pNext
	VkExternalMemoryImageCreateInfoKHR external_memory_image_create_info = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR,
	    .handleTypes = handle_type,
	};

	// In
	VkImageCreateInfo vk_info = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .pNext = &external_memory_image_create_info,
	    .imageType = VK_IMAGE_TYPE_2D,
	    .format = image_format,
	    .extent = {.width = info->width, .height = info->height, .depth = 1},
	    .mipLevels = info->mip_count,
	    .arrayLayers = info->array_size,
	    .samples = VK_SAMPLE_COUNT_1_BIT,
	    .tiling = VK_IMAGE_TILING_OPTIMAL,
	    .usage = image_usage,
	    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	if (0 != (info->create & XRT_SWAPCHAIN_CREATE_PROTECTED_CONTENT)) {
		vk_info.flags |= VK_IMAGE_CREATE_PROTECTED_BIT;
	}

	VkImage image = VK_NULL_HANDLE;
	ret = vk->vkCreateImage(vk->device, &vk_info, NULL, &image);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateImage: %s", vk_result_string(ret));
		// Nothing to cleanup
		return ret;
	}
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
	VkImportMemoryFdInfoKHR import_memory_info = {
	    .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
	    .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
	    .fd = image_native->handle,
	};
#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	VkImportAndroidHardwareBufferInfoANDROID import_memory_info = {
	    .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
	    .pNext = NULL,
	    .buffer = image_native->handle,
	};
#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)
	VkImportMemoryWin32HandleInfoKHR import_memory_info = {
	    .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
	    .pNext = NULL,
	    .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
	    .handle = image_native->handle,
	};
#else
#error "need port"
#endif
	VkMemoryDedicatedAllocateInfoKHR dedicated_memory_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR,
	    .pNext = &import_memory_info,
	    .image = image,
	    .buffer = VK_NULL_HANDLE,
	};
	ret = vk_alloc_and_bind_image_memory(vk, image, image_native->size, &dedicated_memory_info, out_mem, NULL);

	// We have consumed this fd now, make sure it's not freed again.
	image_native->handle = XRT_GRAPHICS_BUFFER_HANDLE_INVALID;

	if (ret != VK_SUCCESS) {
		vk->vkDestroyImage(vk->device, image, NULL);
		return ret;
	}

	*out_image = image;
	return ret;
}

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

VkResult
vk_get_native_handle_from_device_memory(struct vk_bundle *vk,
                                        VkDeviceMemory device_memory,
                                        xrt_graphics_buffer_handle_t *out_handle)
{
	return get_device_memory_handle(vk, device_memory, out_handle);
}

VkResult
vk_create_sampler(struct vk_bundle *vk, VkSamplerAddressMode clamp_mode, VkSampler *out_sampler)
{
	VkSampler sampler;
	VkResult ret;

	VkSamplerCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
	    .magFilter = VK_FILTER_LINEAR,
	    .minFilter = VK_FILTER_LINEAR,
	    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
	    .addressModeU = clamp_mode,
	    .addressModeV = clamp_mode,
	    .addressModeW = clamp_mode,
	    .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
	    .unnormalizedCoordinates = VK_FALSE,
	};

	ret = vk->vkCreateSampler(vk->device, &info, NULL, &sampler);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateSampler: %s", vk_result_string(ret));
		return ret;
	}

	*out_sampler = sampler;

	return VK_SUCCESS;
}

VkResult
vk_create_view(struct vk_bundle *vk,
               VkImage image,
               VkFormat format,
               VkImageSubresourceRange subresource_range,
               VkImageView *out_view)
{
	VkComponentMapping components = {
	    .r = VK_COMPONENT_SWIZZLE_R,
	    .g = VK_COMPONENT_SWIZZLE_G,
	    .b = VK_COMPONENT_SWIZZLE_B,
	    .a = VK_COMPONENT_SWIZZLE_A,
	};

	return vk_create_view_swizzle(vk, image, format, subresource_range, components, out_view);
}

VkResult
vk_create_view_swizzle(struct vk_bundle *vk,
                       VkImage image,
                       VkFormat format,
                       VkImageSubresourceRange subresource_range,
                       VkComponentMapping components,
                       VkImageView *out_view)
{
	VkImageView view;
	VkResult ret;

	VkImageViewCreateInfo imageView = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	    .image = image,
	    .viewType = VK_IMAGE_VIEW_TYPE_2D,
	    .format = format,
	    .components = components,
	    .subresourceRange = subresource_range,
	};

	ret = vk->vkCreateImageView(vk->device, &imageView, NULL, &view);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateImageView: %s", vk_result_string(ret));
		return ret;
	}

	*out_view = view;

	return VK_SUCCESS;
}


/*
 *
 * Command buffer code.
 *
 */

VkResult
vk_init_cmd_buffer(struct vk_bundle *vk, VkCommandBuffer *out_cmd_buffer)
{
	VkCommandBuffer cmd_buffer;
	VkResult ret;

	// Allocate the command buffer.
	VkCommandBufferAllocateInfo cmd_buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	    .commandPool = vk->cmd_pool,
	    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	    .commandBufferCount = 1,
	};

	os_mutex_lock(&vk->cmd_pool_mutex);

	ret = vk->vkAllocateCommandBuffers(vk->device, &cmd_buffer_info, &cmd_buffer);

	os_mutex_unlock(&vk->cmd_pool_mutex);

	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkAllocateCommandBuffers: %s", vk_result_string(ret));
		// Nothing to cleanup
		return ret;
	}

	// Start the command buffer as well.
	VkCommandBufferBeginInfo begin_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};

	os_mutex_lock(&vk->cmd_pool_mutex);
	ret = vk->vkBeginCommandBuffer(cmd_buffer, &begin_info);
	os_mutex_unlock(&vk->cmd_pool_mutex);

	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkBeginCommandBuffer: %s", vk_result_string(ret));
		goto err_buffer;
	}

	*out_cmd_buffer = cmd_buffer;

	return VK_SUCCESS;


err_buffer:
	vk->vkFreeCommandBuffers(vk->device, vk->cmd_pool, 1, &cmd_buffer);

	return ret;
}

VkResult
vk_set_image_layout(struct vk_bundle *vk,
                    VkCommandBuffer cmd_buffer,
                    VkImage image,
                    VkAccessFlags src_access_mask,
                    VkAccessFlags dst_access_mask,
                    VkImageLayout old_layout,
                    VkImageLayout new_layout,
                    VkImageSubresourceRange subresource_range)
{
	VkImageMemoryBarrier barrier = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	    .srcAccessMask = src_access_mask,
	    .dstAccessMask = dst_access_mask,
	    .oldLayout = old_layout,
	    .newLayout = new_layout,
	    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .image = image,
	    .subresourceRange = subresource_range,
	};

	os_mutex_lock(&vk->cmd_pool_mutex);
	vk->vkCmdPipelineBarrier(cmd_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
	                         0, NULL, 0, NULL, 1, &barrier);
	os_mutex_unlock(&vk->cmd_pool_mutex);

	return VK_SUCCESS;
}

VkResult
vk_submit_cmd_buffer(struct vk_bundle *vk, VkCommandBuffer cmd_buffer)
{
	VkResult ret = VK_SUCCESS;
	VkFence fence;
	VkFenceCreateInfo fence_info = {
	    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	};
	VkSubmitInfo submitInfo = {
	    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
	    .commandBufferCount = 1,
	    .pCommandBuffers = &cmd_buffer,
	};

	// Finish the command buffer first.
	os_mutex_lock(&vk->cmd_pool_mutex);
	ret = vk->vkEndCommandBuffer(cmd_buffer);
	os_mutex_unlock(&vk->cmd_pool_mutex);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkEndCommandBuffer: %s", vk_result_string(ret));
		goto out;
	}

	// Create the fence.
	ret = vk->vkCreateFence(vk->device, &fence_info, NULL, &fence);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateFence: %s", vk_result_string(ret));
		goto out;
	}

	// Do the actual submitting.
	ret = vk_locked_submit(vk, vk->queue, 1, &submitInfo, fence);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "Error: Could not submit to queue.\n");
		goto out_fence;
	}

	// Then wait for the fence.
	ret = vk->vkWaitForFences(vk->device, 1, &fence, VK_TRUE, 1000000000);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkWaitForFences: %s", vk_result_string(ret));
		goto out_fence;
	}

	// Yes fall through.

out_fence:
	vk->vkDestroyFence(vk->device, fence, NULL);
out:
	os_mutex_lock(&vk->cmd_pool_mutex);
	vk->vkFreeCommandBuffers(vk->device, vk->cmd_pool, 1, &cmd_buffer);
	os_mutex_unlock(&vk->cmd_pool_mutex);

	return ret;
}

VkAccessFlags
vk_get_access_flags(VkImageLayout layout)
{
	switch (layout) {
	case VK_IMAGE_LAYOUT_UNDEFINED: return 0;
	case VK_IMAGE_LAYOUT_GENERAL: return VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
	case VK_IMAGE_LAYOUT_PREINITIALIZED: return VK_ACCESS_HOST_WRITE_BIT;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return VK_ACCESS_TRANSFER_READ_BIT;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return VK_ACCESS_TRANSFER_WRITE_BIT;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return VK_ACCESS_SHADER_READ_BIT;
	default: U_LOG_E("Unhandled access mask case for layout %d.", layout);
	}
	return 0;
}

bool
vk_init_descriptor_pool(struct vk_bundle *vk,
                        const VkDescriptorPoolSize *pool_sizes,
                        uint32_t pool_size_count,
                        uint32_t set_count,
                        VkDescriptorPool *out_descriptor_pool)
{
	VkDescriptorPoolCreateInfo info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
	    .maxSets = set_count,
	    .poolSizeCount = pool_size_count,
	    .pPoolSizes = pool_sizes,
	};

	VkResult res = vk->vkCreateDescriptorPool(vk->device, &info, NULL, out_descriptor_pool);
	vk_check_error("vkCreateDescriptorPool", res, false);

	return true;
}

bool
vk_allocate_descriptor_sets(struct vk_bundle *vk,
                            VkDescriptorPool descriptor_pool,
                            uint32_t count,
                            const VkDescriptorSetLayout *set_layout,
                            VkDescriptorSet *sets)
{
	VkDescriptorSetAllocateInfo alloc_info = {
	    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
	    .descriptorPool = descriptor_pool,
	    .descriptorSetCount = count,
	    .pSetLayouts = set_layout,
	};

	VkResult res = vk->vkAllocateDescriptorSets(vk->device, &alloc_info, sets);
	vk_check_error("vkAllocateDescriptorSets", res, false);

	return true;
}

bool
vk_buffer_init(struct vk_bundle *vk,
               VkDeviceSize size,
               VkBufferUsageFlags usage,
               VkMemoryPropertyFlags properties,
               VkBuffer *out_buffer,
               VkDeviceMemory *out_mem)
{
	VkBufferCreateInfo buffer_info = {
	    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .size = size,
	    .usage = usage,
	};

	VkResult res = vk->vkCreateBuffer(vk->device, &buffer_info, NULL, out_buffer);
	vk_check_error("vkCreateBuffer", res, false);

	VkMemoryRequirements requirements;
	vk->vkGetBufferMemoryRequirements(vk->device, *out_buffer, &requirements);

	VkMemoryAllocateInfo alloc_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	    .allocationSize = requirements.size,
	};

	if (!vk_get_memory_type(vk, requirements.memoryTypeBits, properties, &alloc_info.memoryTypeIndex)) {
		VK_ERROR(vk, "Failed to find matching memoryTypeIndex for buffer");
		return false;
	}

	res = vk->vkAllocateMemory(vk->device, &alloc_info, NULL, out_mem);
	vk_check_error("vkAllocateMemory", res, false);

	res = vk->vkBindBufferMemory(vk->device, *out_buffer, *out_mem, 0);
	vk_check_error("vkBindBufferMemory", res, false);

	return true;
}

void
vk_buffer_destroy(struct vk_buffer *self, struct vk_bundle *vk)
{
	vk->vkDestroyBuffer(vk->device, self->handle, NULL);
	vk->vkFreeMemory(vk->device, self->memory, NULL);
}

bool
vk_update_buffer(struct vk_bundle *vk, float *buffer, size_t buffer_size, VkDeviceMemory memory)
{
	void *tmp;
	VkResult res = vk->vkMapMemory(vk->device, memory, 0, VK_WHOLE_SIZE, 0, &tmp);
	vk_check_error("vkMapMemory", res, false);

	memcpy(tmp, buffer, buffer_size);

	VkMappedMemoryRange memory_range = {
	    .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
	    .memory = memory,
	    .size = VK_WHOLE_SIZE,
	};

	res = vk->vkFlushMappedMemoryRanges(vk->device, 1, &memory_range);
	vk_check_error("vkFlushMappedMemoryRanges", res, false);

	vk->vkUnmapMemory(vk->device, memory);

	return true;
}

VkResult
vk_locked_submit(struct vk_bundle *vk, VkQueue queue, uint32_t count, const VkSubmitInfo *infos, VkFence fence)
{
	VkResult ret;
	os_mutex_lock(&vk->queue_mutex);
	os_mutex_lock(&vk->cmd_pool_mutex);
	ret = vk->vkQueueSubmit(queue, count, infos, fence);
	os_mutex_unlock(&vk->cmd_pool_mutex);
	os_mutex_unlock(&vk->queue_mutex);
	return ret;
}

void
vk_insert_image_memory_barrier(struct vk_bundle *vk,
                               VkCommandBuffer cmdbuffer,
                               VkImage image,
                               VkAccessFlags srcAccessMask,
                               VkAccessFlags dstAccessMask,
                               VkImageLayout oldImageLayout,
                               VkImageLayout newImageLayout,
                               VkPipelineStageFlags srcStageMask,
                               VkPipelineStageFlags dstStageMask,
                               VkImageSubresourceRange subresourceRange)
{

	VkImageMemoryBarrier image_memory_barrier = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	    .srcAccessMask = srcAccessMask,
	    .dstAccessMask = dstAccessMask,
	    .oldLayout = oldImageLayout,
	    .newLayout = newImageLayout,
	    .image = image,
	    .subresourceRange = subresourceRange,
	};

	vk->vkCmdPipelineBarrier(   //
	    cmdbuffer,              // commandBuffer
	    srcStageMask,           // srcStageMask
	    dstStageMask,           // dstStageMask
	    0,                      // dependencyFlags
	    0,                      // memoryBarrierCount
	    NULL,                   // pMemoryBarriers
	    0,                      // bufferMemoryBarrierCount
	    NULL,                   // pBufferMemoryBarriers
	    1,                      // imageMemoryBarrierCount
	    &image_memory_barrier); // pImageMemoryBarriers
}
