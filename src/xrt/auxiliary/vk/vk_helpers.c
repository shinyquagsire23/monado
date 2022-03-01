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
vk_color_format_string(VkFormat code)
{
	switch (code) {
		ENUM_TO_STR(VK_FORMAT_B8G8R8A8_UNORM);
		ENUM_TO_STR(VK_FORMAT_UNDEFINED);
		ENUM_TO_STR(VK_FORMAT_R8G8B8A8_SRGB);
		ENUM_TO_STR(VK_FORMAT_B8G8R8A8_SRGB);
		ENUM_TO_STR(VK_FORMAT_R8G8B8_SRGB);
		ENUM_TO_STR(VK_FORMAT_B8G8R8_SRGB);
		ENUM_TO_STR(VK_FORMAT_R5G6B5_UNORM_PACK16);
		ENUM_TO_STR(VK_FORMAT_B5G6R5_UNORM_PACK16);
		ENUM_TO_STR(VK_FORMAT_D32_SFLOAT_S8_UINT);
		ENUM_TO_STR(VK_FORMAT_D32_SFLOAT);
		ENUM_TO_STR(VK_FORMAT_D24_UNORM_S8_UINT);
		ENUM_TO_STR(VK_FORMAT_D16_UNORM_S8_UINT);
		ENUM_TO_STR(VK_FORMAT_D16_UNORM);
		ENUM_TO_STR(VK_FORMAT_A2B10G10R10_UNORM_PACK32);
		ENUM_TO_STR(VK_FORMAT_R16G16B16A16_SFLOAT);
		ENUM_TO_STR(VK_FORMAT_R16G16B16A16_UNORM);
		ENUM_TO_STR(VK_FORMAT_R8G8B8A8_UNORM);
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
 * Internal helper functions.
 *
 */

static bool
is_fence_bit_supported(struct vk_bundle *vk, VkExternalFenceHandleTypeFlagBits handle_type)
{
	VkPhysicalDeviceExternalFenceInfo external_fence_info = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO,
	    .handleType = handle_type,
	};
	VkExternalFenceProperties external_fence_props = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES,
	};

	vk->vkGetPhysicalDeviceExternalFencePropertiesKHR( //
	    vk->physical_device,                           // physicalDevice
	    &external_fence_info,                          // pExternalFenceInfo
	    &external_fence_props);                        // pExternalFenceProperties

	const VkExternalFenceFeatureFlagBits bits =    //
	    VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT | //
	    VK_EXTERNAL_FENCE_FEATURE_IMPORTABLE_BIT;  //

	VkExternalFenceFeatureFlagBits masked = bits & external_fence_props.externalFenceFeatures;
	if (masked != bits) {
		// All must be supported.
		return false;
	}

	return true;
}

static void
fill_in_external_object_properties(struct vk_bundle *vk)
{
	if (vk->vkGetPhysicalDeviceExternalFencePropertiesKHR == NULL) {
		VK_WARN(vk, "vkGetPhysicalDeviceExternalFencePropertiesKHR not supported, should always be.");
		return;
	}

#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	vk->external.fence_sync_fd = is_fence_bit_supported(vk, VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT);
	vk->external.fence_opaque_fd = is_fence_bit_supported(vk, VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT);

	VK_DEBUG(vk, "Supported fences:\n\t%s: %s\n\t%s: %s",      //
	         "VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT",      //
	         vk->external.fence_sync_fd ? "true" : "false",    //
	         "VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT",    //
	         vk->external.fence_opaque_fd ? "true" : "false"); //
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	vk->external.fence_win32_handle = is_fence_bit_supported(vk, VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_BIT);

	VK_DEBUG(vk, "Supported fences:\n\t%s: %s",                   //
	         "VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_WIN32_BIT",    //
	         vk->external.fence_win32_handle ? "true" : "false"); //
#else
#error "Need port for fence sync handles checkers"
#endif
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

	VkImage image = VK_NULL_HANDLE;
	VkResult ret = VK_SUCCESS;

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
	VkExternalMemoryImageCreateInfoKHR external_memory_image_create_info = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR,
	    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
	};
#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	VkExternalMemoryImageCreateInfoKHR external_memory_image_create_info = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR,
	    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
	};
#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)
	VkExternalMemoryImageCreateInfoKHR external_memory_image_create_info = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR,
	    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
	};
#else
#error "need port"
#endif

	VkPhysicalDeviceExternalImageFormatInfo external_image_format_info = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
	    .handleType = external_memory_image_create_info.handleTypes,
	};

	VkPhysicalDeviceImageFormatInfo2 format_info = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
	    .pNext = &external_image_format_info,
	    .format = image_format,
	    .type = VK_IMAGE_TYPE_2D,
	    .tiling = VK_IMAGE_TILING_OPTIMAL,
	    .usage = image_usage,
	};

	VkExternalImageFormatProperties external_format_properties = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
	};

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

VkResult
vk_init_cmd_pool(struct vk_bundle *vk)
{
	VkCommandPoolCreateInfo cmd_pool_info = {
	    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
	    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	    .queueFamilyIndex = vk->queue_family_index,
	};

	VkResult ret;
	ret = vk->vkCreateCommandPool(vk->device, &cmd_pool_info, NULL, &vk->cmd_pool);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateCommandPool: %s", vk_result_string(ret));
	}

	return ret;
}

/*
 *
 * Function getting code.
 *
 */

#define GET_PROC(vk, name) (PFN_##name) vk->vkGetInstanceProcAddr(NULL, #name);

#define GET_INS_PROC(vk, name) (PFN_##name) vk->vkGetInstanceProcAddr(vk->instance, #name);

#define GET_DEV_PROC(vk, name) (PFN_##name) vk->vkGetDeviceProcAddr(vk->device, #name);

VkResult
vk_get_loader_functions(struct vk_bundle *vk, PFN_vkGetInstanceProcAddr g)
{
	vk->vkGetInstanceProcAddr = g;

	// Fill in all loader functions.
	// clang-format off
	vk->vkCreateInstance = GET_PROC(vk, vkCreateInstance);
	vk->vkEnumerateInstanceExtensionProperties = GET_PROC(vk, vkEnumerateInstanceExtensionProperties);
	// clang-format on

	return VK_SUCCESS;
}

VkResult
vk_get_instance_functions(struct vk_bundle *vk)
{
	// clang-format off
	// beginning of GENERATED instance loader code - do not modify - used by scripts
	vk->vkDestroyInstance                                 = GET_INS_PROC(vk, vkDestroyInstance);
	vk->vkGetDeviceProcAddr                               = GET_INS_PROC(vk, vkGetDeviceProcAddr);
	vk->vkCreateDevice                                    = GET_INS_PROC(vk, vkCreateDevice);
	vk->vkDestroySurfaceKHR                               = GET_INS_PROC(vk, vkDestroySurfaceKHR);

	vk->vkCreateDebugReportCallbackEXT                    = GET_INS_PROC(vk, vkCreateDebugReportCallbackEXT);
	vk->vkDestroyDebugReportCallbackEXT                   = GET_INS_PROC(vk, vkDestroyDebugReportCallbackEXT);

	vk->vkEnumeratePhysicalDevices                        = GET_INS_PROC(vk, vkEnumeratePhysicalDevices);
	vk->vkGetPhysicalDeviceProperties                     = GET_INS_PROC(vk, vkGetPhysicalDeviceProperties);
	vk->vkGetPhysicalDeviceProperties2                    = GET_INS_PROC(vk, vkGetPhysicalDeviceProperties2);
	vk->vkGetPhysicalDeviceFeatures2                      = GET_INS_PROC(vk, vkGetPhysicalDeviceFeatures2);
	vk->vkGetPhysicalDeviceMemoryProperties               = GET_INS_PROC(vk, vkGetPhysicalDeviceMemoryProperties);
	vk->vkGetPhysicalDeviceQueueFamilyProperties          = GET_INS_PROC(vk, vkGetPhysicalDeviceQueueFamilyProperties);
	vk->vkGetPhysicalDeviceSurfaceCapabilitiesKHR         = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
	vk->vkGetPhysicalDeviceSurfaceFormatsKHR              = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfaceFormatsKHR);
	vk->vkGetPhysicalDeviceSurfacePresentModesKHR         = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfacePresentModesKHR);
	vk->vkGetPhysicalDeviceSurfaceSupportKHR              = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfaceSupportKHR);
	vk->vkGetPhysicalDeviceFormatProperties               = GET_INS_PROC(vk, vkGetPhysicalDeviceFormatProperties);
	vk->vkGetPhysicalDeviceImageFormatProperties2         = GET_INS_PROC(vk, vkGetPhysicalDeviceImageFormatProperties2);
	vk->vkGetPhysicalDeviceExternalBufferPropertiesKHR    = GET_INS_PROC(vk, vkGetPhysicalDeviceExternalBufferPropertiesKHR);
	vk->vkGetPhysicalDeviceExternalFencePropertiesKHR     = GET_INS_PROC(vk, vkGetPhysicalDeviceExternalFencePropertiesKHR);
	vk->vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = GET_INS_PROC(vk, vkGetPhysicalDeviceExternalSemaphorePropertiesKHR);
	vk->vkEnumerateDeviceExtensionProperties              = GET_INS_PROC(vk, vkEnumerateDeviceExtensionProperties);

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
	vk->vkCreateDisplayPlaneSurfaceKHR                    = GET_INS_PROC(vk, vkCreateDisplayPlaneSurfaceKHR);
	vk->vkGetDisplayPlaneCapabilitiesKHR                  = GET_INS_PROC(vk, vkGetDisplayPlaneCapabilitiesKHR);
	vk->vkGetPhysicalDeviceDisplayPropertiesKHR           = GET_INS_PROC(vk, vkGetPhysicalDeviceDisplayPropertiesKHR);
	vk->vkGetPhysicalDeviceDisplayPlanePropertiesKHR      = GET_INS_PROC(vk, vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
	vk->vkGetDisplayModePropertiesKHR                     = GET_INS_PROC(vk, vkGetDisplayModePropertiesKHR);
	vk->vkReleaseDisplayEXT                               = GET_INS_PROC(vk, vkReleaseDisplayEXT);

#endif // defined(VK_USE_PLATFORM_DISPLAY_KHR)

#if defined(VK_USE_PLATFORM_XCB_KHR)
	vk->vkCreateXcbSurfaceKHR                             = GET_INS_PROC(vk, vkCreateXcbSurfaceKHR);

#endif // defined(VK_USE_PLATFORM_XCB_KHR)

#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	vk->vkCreateWaylandSurfaceKHR                         = GET_INS_PROC(vk, vkCreateWaylandSurfaceKHR);

#endif // defined(VK_USE_PLATFORM_WAYLAND_KHR)

#if defined(VK_USE_PLATFORM_WAYLAND_KHR) && defined(VK_EXT_acquire_drm_display)
	vk->vkAcquireDrmDisplayEXT                            = GET_INS_PROC(vk, vkAcquireDrmDisplayEXT);
	vk->vkGetDrmDisplayEXT                                = GET_INS_PROC(vk, vkGetDrmDisplayEXT);

#endif // defined(VK_USE_PLATFORM_WAYLAND_KHR) && defined(VK_EXT_acquire_drm_display)

#if defined(VK_USE_PLATFORM_XLIB_XRANDR_EXT)
	vk->vkGetRandROutputDisplayEXT                        = GET_INS_PROC(vk, vkGetRandROutputDisplayEXT);
	vk->vkAcquireXlibDisplayEXT                           = GET_INS_PROC(vk, vkAcquireXlibDisplayEXT);

#endif // defined(VK_USE_PLATFORM_XLIB_XRANDR_EXT)

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	vk->vkCreateAndroidSurfaceKHR                         = GET_INS_PROC(vk, vkCreateAndroidSurfaceKHR);

#endif // defined(VK_USE_PLATFORM_ANDROID_KHR)

#if defined(VK_USE_PLATFORM_WIN32_KHR)
	vk->vkCreateWin32SurfaceKHR                           = GET_INS_PROC(vk, vkCreateWin32SurfaceKHR);

#endif // defined(VK_USE_PLATFORM_WIN32_KHR)

#if defined(VK_EXT_display_surface_counter)
	vk->vkGetPhysicalDeviceSurfaceCapabilities2EXT        = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfaceCapabilities2EXT);
#endif // defined(VK_EXT_display_surface_counter)

	// end of GENERATED instance loader code - do not modify - used by scripts

	// clang-format on
	return VK_SUCCESS;
}

static VkResult
vk_get_device_functions(struct vk_bundle *vk)
{

	// clang-format off
	// beginning of GENERATED device loader code - do not modify - used by scripts
	vk->vkDestroyDevice                             = GET_DEV_PROC(vk, vkDestroyDevice);
	vk->vkDeviceWaitIdle                            = GET_DEV_PROC(vk, vkDeviceWaitIdle);
	vk->vkAllocateMemory                            = GET_DEV_PROC(vk, vkAllocateMemory);
	vk->vkFreeMemory                                = GET_DEV_PROC(vk, vkFreeMemory);
	vk->vkMapMemory                                 = GET_DEV_PROC(vk, vkMapMemory);
	vk->vkUnmapMemory                               = GET_DEV_PROC(vk, vkUnmapMemory);

	vk->vkCreateBuffer                              = GET_DEV_PROC(vk, vkCreateBuffer);
	vk->vkDestroyBuffer                             = GET_DEV_PROC(vk, vkDestroyBuffer);
	vk->vkBindBufferMemory                          = GET_DEV_PROC(vk, vkBindBufferMemory);

	vk->vkCreateImage                               = GET_DEV_PROC(vk, vkCreateImage);
	vk->vkDestroyImage                              = GET_DEV_PROC(vk, vkDestroyImage);
	vk->vkBindImageMemory                           = GET_DEV_PROC(vk, vkBindImageMemory);

	vk->vkGetBufferMemoryRequirements               = GET_DEV_PROC(vk, vkGetBufferMemoryRequirements);
	vk->vkFlushMappedMemoryRanges                   = GET_DEV_PROC(vk, vkFlushMappedMemoryRanges);
	vk->vkGetImageMemoryRequirements                = GET_DEV_PROC(vk, vkGetImageMemoryRequirements);
	vk->vkGetImageMemoryRequirements2               = GET_DEV_PROC(vk, vkGetImageMemoryRequirements2KHR);
	vk->vkGetImageSubresourceLayout                 = GET_DEV_PROC(vk, vkGetImageSubresourceLayout);

	vk->vkCreateImageView                           = GET_DEV_PROC(vk, vkCreateImageView);
	vk->vkDestroyImageView                          = GET_DEV_PROC(vk, vkDestroyImageView);

	vk->vkCreateSampler                             = GET_DEV_PROC(vk, vkCreateSampler);
	vk->vkDestroySampler                            = GET_DEV_PROC(vk, vkDestroySampler);

	vk->vkCreateShaderModule                        = GET_DEV_PROC(vk, vkCreateShaderModule);
	vk->vkDestroyShaderModule                       = GET_DEV_PROC(vk, vkDestroyShaderModule);

	vk->vkCreateCommandPool                         = GET_DEV_PROC(vk, vkCreateCommandPool);
	vk->vkDestroyCommandPool                        = GET_DEV_PROC(vk, vkDestroyCommandPool);

	vk->vkAllocateCommandBuffers                    = GET_DEV_PROC(vk, vkAllocateCommandBuffers);
	vk->vkBeginCommandBuffer                        = GET_DEV_PROC(vk, vkBeginCommandBuffer);
	vk->vkCmdPipelineBarrier                        = GET_DEV_PROC(vk, vkCmdPipelineBarrier);
	vk->vkCmdBeginRenderPass                        = GET_DEV_PROC(vk, vkCmdBeginRenderPass);
	vk->vkCmdSetScissor                             = GET_DEV_PROC(vk, vkCmdSetScissor);
	vk->vkCmdSetViewport                            = GET_DEV_PROC(vk, vkCmdSetViewport);
	vk->vkCmdClearColorImage                        = GET_DEV_PROC(vk, vkCmdClearColorImage);
	vk->vkCmdEndRenderPass                          = GET_DEV_PROC(vk, vkCmdEndRenderPass);
	vk->vkCmdBindDescriptorSets                     = GET_DEV_PROC(vk, vkCmdBindDescriptorSets);
	vk->vkCmdBindPipeline                           = GET_DEV_PROC(vk, vkCmdBindPipeline);
	vk->vkCmdBindVertexBuffers                      = GET_DEV_PROC(vk, vkCmdBindVertexBuffers);
	vk->vkCmdBindIndexBuffer                        = GET_DEV_PROC(vk, vkCmdBindIndexBuffer);
	vk->vkCmdDraw                                   = GET_DEV_PROC(vk, vkCmdDraw);
	vk->vkCmdDrawIndexed                            = GET_DEV_PROC(vk, vkCmdDrawIndexed);
	vk->vkCmdDispatch                               = GET_DEV_PROC(vk, vkCmdDispatch);
	vk->vkCmdCopyBuffer                             = GET_DEV_PROC(vk, vkCmdCopyBuffer);
	vk->vkCmdCopyBufferToImage                      = GET_DEV_PROC(vk, vkCmdCopyBufferToImage);
	vk->vkCmdCopyImage                              = GET_DEV_PROC(vk, vkCmdCopyImage);
	vk->vkCmdCopyImageToBuffer                      = GET_DEV_PROC(vk, vkCmdCopyImageToBuffer);
	vk->vkCmdBlitImage                              = GET_DEV_PROC(vk, vkCmdBlitImage);
	vk->vkEndCommandBuffer                          = GET_DEV_PROC(vk, vkEndCommandBuffer);
	vk->vkFreeCommandBuffers                        = GET_DEV_PROC(vk, vkFreeCommandBuffers);

	vk->vkCreateRenderPass                          = GET_DEV_PROC(vk, vkCreateRenderPass);
	vk->vkDestroyRenderPass                         = GET_DEV_PROC(vk, vkDestroyRenderPass);

	vk->vkCreateFramebuffer                         = GET_DEV_PROC(vk, vkCreateFramebuffer);
	vk->vkDestroyFramebuffer                        = GET_DEV_PROC(vk, vkDestroyFramebuffer);

	vk->vkCreatePipelineCache                       = GET_DEV_PROC(vk, vkCreatePipelineCache);
	vk->vkDestroyPipelineCache                      = GET_DEV_PROC(vk, vkDestroyPipelineCache);

	vk->vkResetDescriptorPool                       = GET_DEV_PROC(vk, vkResetDescriptorPool);
	vk->vkCreateDescriptorPool                      = GET_DEV_PROC(vk, vkCreateDescriptorPool);
	vk->vkDestroyDescriptorPool                     = GET_DEV_PROC(vk, vkDestroyDescriptorPool);

	vk->vkAllocateDescriptorSets                    = GET_DEV_PROC(vk, vkAllocateDescriptorSets);
	vk->vkFreeDescriptorSets                        = GET_DEV_PROC(vk, vkFreeDescriptorSets);

	vk->vkCreateComputePipelines                    = GET_DEV_PROC(vk, vkCreateComputePipelines);
	vk->vkCreateGraphicsPipelines                   = GET_DEV_PROC(vk, vkCreateGraphicsPipelines);
	vk->vkDestroyPipeline                           = GET_DEV_PROC(vk, vkDestroyPipeline);

	vk->vkCreatePipelineLayout                      = GET_DEV_PROC(vk, vkCreatePipelineLayout);
	vk->vkDestroyPipelineLayout                     = GET_DEV_PROC(vk, vkDestroyPipelineLayout);

	vk->vkCreateDescriptorSetLayout                 = GET_DEV_PROC(vk, vkCreateDescriptorSetLayout);
	vk->vkUpdateDescriptorSets                      = GET_DEV_PROC(vk, vkUpdateDescriptorSets);
	vk->vkDestroyDescriptorSetLayout                = GET_DEV_PROC(vk, vkDestroyDescriptorSetLayout);

	vk->vkGetDeviceQueue                            = GET_DEV_PROC(vk, vkGetDeviceQueue);
	vk->vkQueueSubmit                               = GET_DEV_PROC(vk, vkQueueSubmit);
	vk->vkQueueWaitIdle                             = GET_DEV_PROC(vk, vkQueueWaitIdle);

	vk->vkCreateSemaphore                           = GET_DEV_PROC(vk, vkCreateSemaphore);
#if defined(VK_KHR_timeline_semaphore)
	vk->vkSignalSemaphore                           = GET_DEV_PROC(vk, vkSignalSemaphoreKHR);
	vk->vkWaitSemaphores                            = GET_DEV_PROC(vk, vkWaitSemaphoresKHR);
	vk->vkGetSemaphoreCounterValue                  = GET_DEV_PROC(vk, vkGetSemaphoreCounterValueKHR);
#endif // defined(VK_KHR_timeline_semaphore)

	vk->vkDestroySemaphore                          = GET_DEV_PROC(vk, vkDestroySemaphore);

	vk->vkCreateFence                               = GET_DEV_PROC(vk, vkCreateFence);
	vk->vkWaitForFences                             = GET_DEV_PROC(vk, vkWaitForFences);
	vk->vkGetFenceStatus                            = GET_DEV_PROC(vk, vkGetFenceStatus);
	vk->vkDestroyFence                              = GET_DEV_PROC(vk, vkDestroyFence);
	vk->vkResetFences                               = GET_DEV_PROC(vk, vkResetFences);

	vk->vkCreateSwapchainKHR                        = GET_DEV_PROC(vk, vkCreateSwapchainKHR);
	vk->vkDestroySwapchainKHR                       = GET_DEV_PROC(vk, vkDestroySwapchainKHR);
	vk->vkGetSwapchainImagesKHR                     = GET_DEV_PROC(vk, vkGetSwapchainImagesKHR);
	vk->vkAcquireNextImageKHR                       = GET_DEV_PROC(vk, vkAcquireNextImageKHR);
	vk->vkQueuePresentKHR                           = GET_DEV_PROC(vk, vkQueuePresentKHR);

#if defined(VK_USE_PLATFORM_WIN32_KHR)
	vk->vkGetMemoryWin32HandleKHR                   = GET_DEV_PROC(vk, vkGetMemoryWin32HandleKHR);
	vk->vkImportSemaphoreWin32HandleKHR             = GET_DEV_PROC(vk, vkImportSemaphoreWin32HandleKHR);
	vk->vkImportFenceWin32HandleKHR                 = GET_DEV_PROC(vk, vkImportFenceWin32HandleKHR);
	vk->vkGetFenceWin32HandleKHR                    = GET_DEV_PROC(vk, vkGetFenceWin32HandleKHR);
#endif // defined(VK_USE_PLATFORM_WIN32_KHR)

#if !defined(VK_USE_PLATFORM_WIN32_KHR)
	vk->vkGetMemoryFdKHR                            = GET_DEV_PROC(vk, vkGetMemoryFdKHR);

	vk->vkImportSemaphoreFdKHR                      = GET_DEV_PROC(vk, vkImportSemaphoreFdKHR);
	vk->vkGetSemaphoreFdKHR                         = GET_DEV_PROC(vk, vkGetSemaphoreFdKHR);

	vk->vkImportFenceFdKHR                          = GET_DEV_PROC(vk, vkImportFenceFdKHR);
	vk->vkGetFenceFdKHR                             = GET_DEV_PROC(vk, vkGetFenceFdKHR);
#endif // !defined(VK_USE_PLATFORM_WIN32_KHR)

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	vk->vkGetMemoryAndroidHardwareBufferANDROID     = GET_DEV_PROC(vk, vkGetMemoryAndroidHardwareBufferANDROID);
	vk->vkGetAndroidHardwareBufferPropertiesANDROID = GET_DEV_PROC(vk, vkGetAndroidHardwareBufferPropertiesANDROID);

#endif // defined(VK_USE_PLATFORM_ANDROID_KHR)

	vk->vkGetPastPresentationTimingGOOGLE           = GET_DEV_PROC(vk, vkGetPastPresentationTimingGOOGLE);

#if defined(VK_EXT_display_control)
	vk->vkGetSwapchainCounterEXT                    = GET_DEV_PROC(vk, vkGetSwapchainCounterEXT);
	vk->vkRegisterDeviceEventEXT                    = GET_DEV_PROC(vk, vkRegisterDeviceEventEXT);
	vk->vkRegisterDisplayEventEXT                   = GET_DEV_PROC(vk, vkRegisterDisplayEventEXT);
#endif // defined(VK_EXT_display_control)

	// end of GENERATED device loader code - do not modify - used by scripts
	// clang-format on
	return VK_SUCCESS;
}


static void
vk_print_device_info_debug(struct vk_bundle *vk, VkPhysicalDeviceProperties *pdp, uint32_t gpu_index, const char *title)
{
	VK_DEBUG(vk,
	         "%s"
	         "\tname: %s\n"
	         "\tvendor: 0x%04x\n"
	         "\tproduct: 0x%04x\n"
	         "\tapiVersion: %u.%u.%u\n"
	         "\tdriverVersion: %u.%u.%u",
	         title, pdp->deviceName, pdp->vendorID, pdp->deviceID, VK_VERSION_MAJOR(pdp->apiVersion),
	         VK_VERSION_MINOR(pdp->apiVersion), VK_VERSION_PATCH(pdp->apiVersion),
	         VK_VERSION_MAJOR(pdp->driverVersion), VK_VERSION_MINOR(pdp->driverVersion),
	         VK_VERSION_PATCH(pdp->driverVersion));
}

/*
 *
 * Creation code.
 *
 */

static VkResult
vk_select_physical_device(struct vk_bundle *vk, int forced_index)
{
	VkPhysicalDevice physical_devices[16];
	uint32_t gpu_count = ARRAY_SIZE(physical_devices);
	VkResult ret;

	ret = vk->vkEnumeratePhysicalDevices(vk->instance, &gpu_count, physical_devices);
	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "vkEnumeratePhysicalDevices: %s", vk_result_string(ret));
		return ret;
	}

	if (gpu_count < 1) {
		VK_DEBUG(vk, "No physical device found!");
		return VK_ERROR_DEVICE_LOST;
	}

	if (gpu_count > 1) {
		VK_DEBUG(vk, "Can not deal well with multiple devices.");
	}

	VK_DEBUG(vk, "Choosing Vulkan device index");
	uint32_t gpu_index = 0;
	if (forced_index > -1) {
		if ((uint32_t)forced_index + 1 > gpu_count) {
			VK_ERROR(vk, "Attempted to force GPU index %d, but only %d GPUs are available", forced_index,
			         gpu_count);
			return VK_ERROR_DEVICE_LOST;
		}
		gpu_index = forced_index;
		VK_DEBUG(vk, "Forced use of Vulkan device index %d.", gpu_index);
	} else {
		VK_DEBUG(vk, "Available GPUs");
		// as a first-step to 'intelligent' selection, prefer a
		// 'discrete' gpu if it is present
		for (uint32_t i = 0; i < gpu_count; i++) {
			VkPhysicalDeviceProperties pdp;
			vk->vkGetPhysicalDeviceProperties(physical_devices[i], &pdp);

			char title[20];
			snprintf(title, 20, "GPU index %d\n", i);
			vk_print_device_info_debug(vk, &pdp, i, title);

			if (pdp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
				gpu_index = i;
			}
		}
	}

	vk->physical_device = physical_devices[gpu_index];
	vk->physical_device_index = gpu_index;

	VkPhysicalDeviceProperties pdp;
	vk->vkGetPhysicalDeviceProperties(physical_devices[gpu_index], &pdp);

	char title[20];
	snprintf(title, 20, "Selected GPU: %d\n", gpu_index);
	vk_print_device_info_debug(vk, &pdp, gpu_index, title);

	char *tegra_substr = strstr(pdp.deviceName, "Tegra");
	if (tegra_substr) {
		vk->is_tegra = true;
		VK_DEBUG(vk, "Detected Tegra, using Tegra specific workarounds!");
	}

	// Fill out the device memory props as well.
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical_device, &vk->device_memory_props);

	return VK_SUCCESS;
}

static VkResult
vk_find_graphics_queue(struct vk_bundle *vk, uint32_t *out_graphics_queue)
{
	/* Find the first graphics queue */
	uint32_t queue_count = 0;
	uint32_t i = 0;
	vk->vkGetPhysicalDeviceQueueFamilyProperties(vk->physical_device, &queue_count, NULL);

	VkQueueFamilyProperties *queue_family_props = U_TYPED_ARRAY_CALLOC(VkQueueFamilyProperties, queue_count);

	vk->vkGetPhysicalDeviceQueueFamilyProperties(vk->physical_device, &queue_count, queue_family_props);

	if (queue_count == 0) {
		VK_DEBUG(vk, "Failed to get queue properties");
		goto err_free;
	}

	for (i = 0; i < queue_count; i++) {
		if (queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			break;
		}
	}

	if (i >= queue_count) {
		VK_DEBUG(vk, "No graphics queue found");
		goto err_free;
	}

	*out_graphics_queue = i;

	free(queue_family_props);

	return VK_SUCCESS;

err_free:
	free(queue_family_props);
	return VK_ERROR_INITIALIZATION_FAILED;
}

static VkResult
vk_find_compute_only_queue(struct vk_bundle *vk, uint32_t *out_compute_queue)
{
	/* Find the first graphics queue */
	uint32_t queue_count = 0;
	uint32_t i = 0;
	vk->vkGetPhysicalDeviceQueueFamilyProperties(vk->physical_device, &queue_count, NULL);

	VkQueueFamilyProperties *queue_family_props = U_TYPED_ARRAY_CALLOC(VkQueueFamilyProperties, queue_count);

	vk->vkGetPhysicalDeviceQueueFamilyProperties(vk->physical_device, &queue_count, queue_family_props);

	if (queue_count == 0) {
		VK_DEBUG(vk, "Failed to get queue properties");
		goto err_free;
	}

	for (i = 0; i < queue_count; i++) {
		if (queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			continue;
		}

		if (queue_family_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
			break;
		}
	}

	if (i >= queue_count) {
		VK_DEBUG(vk, "No compute only queue found");
		goto err_free;
	}

	*out_compute_queue = i;

	free(queue_family_props);

	return VK_SUCCESS;

err_free:
	free(queue_family_props);

	return VK_ERROR_INITIALIZATION_FAILED;
}

static bool
vk_check_extension(struct vk_bundle *vk, VkExtensionProperties *props, uint32_t prop_count, const char *ext)
{
	for (uint32_t i = 0; i < prop_count; i++) {
		if (strcmp(props[i].extensionName, ext) == 0) {
			return true;
		}
	}

	return false;
}

void
vk_fill_in_has_instance_extensions(struct vk_bundle *vk, struct u_string_list *ext_list)
{
	// beginning of GENERATED instance extension code - do not modify - used by scripts
	// Reset before filling out.
	vk->has_EXT_display_surface_counter = false;

	const char *const *exts = u_string_list_get_data(ext_list);
	uint32_t ext_count = u_string_list_get_size(ext_list);

	for (uint32_t i = 0; i < ext_count; i++) {
		const char *ext = exts[i];

#if defined(VK_EXT_display_surface_counter)
		if (strcmp(ext, VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME) == 0) {
			vk->has_EXT_display_surface_counter = true;
			continue;
		}
#endif // defined(VK_EXT_display_surface_counter)
	}
	// end of GENERATED instance extension code - do not modify - used by scripts
}

static void
fill_in_has_device_extensions(struct vk_bundle *vk, struct u_string_list *ext_list)
{
	// beginning of GENERATED device extension code - do not modify - used by scripts
	// Reset before filling out.
	vk->has_KHR_timeline_semaphore = false;
	vk->has_EXT_global_priority = false;
	vk->has_EXT_robustness2 = false;
	vk->has_GOOGLE_display_timing = false;
	vk->has_EXT_display_control = false;

	const char *const *exts = u_string_list_get_data(ext_list);
	uint32_t ext_count = u_string_list_get_size(ext_list);

	for (uint32_t i = 0; i < ext_count; i++) {
		const char *ext = exts[i];

#if defined(VK_KHR_timeline_semaphore)
		if (strcmp(ext, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0) {
			vk->has_KHR_timeline_semaphore = true;
			continue;
		}
#endif // defined(VK_KHR_timeline_semaphore)

#if defined(VK_EXT_global_priority)
		if (strcmp(ext, VK_EXT_GLOBAL_PRIORITY_EXTENSION_NAME) == 0) {
			vk->has_EXT_global_priority = true;
			continue;
		}
#endif // defined(VK_EXT_global_priority)

#if defined(VK_EXT_robustness2)
		if (strcmp(ext, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME) == 0) {
			vk->has_EXT_robustness2 = true;
			continue;
		}
#endif // defined(VK_EXT_robustness2)

#if defined(VK_GOOGLE_display_timing)
		if (strcmp(ext, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME) == 0) {
			vk->has_GOOGLE_display_timing = true;
			continue;
		}
#endif // defined(VK_GOOGLE_display_timing)

#if defined(VK_EXT_display_control)
		if (strcmp(ext, VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME) == 0) {
			vk->has_EXT_display_control = true;
			continue;
		}
#endif // defined(VK_EXT_display_control)
	}
	// end of GENERATED device extension code - do not modify - used by scripts
}

static bool
vk_should_skip_optional_instance_ext(struct vk_bundle *vk,
                                     struct u_string_list *required_instance_ext_list,
                                     struct u_string_list *optional_instance_ext_listconst,
                                     const char *ext)
{
#ifdef VK_EXT_display_surface_counter
	if (strcmp(ext, VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME) == 0) {
		// it does not make sense to enable surface counter on anything that does not use a VkDisplayKHR
		if (!u_string_list_contains(required_instance_ext_list, VK_KHR_DISPLAY_EXTENSION_NAME)) {
			VK_DEBUG(vk, "Skipping optional instance extension %s because %s is not enabled", ext,
			         VK_KHR_DISPLAY_EXTENSION_NAME);
			return true;
		}
		VK_DEBUG(vk, "Not skipping optional instance extension %s because %s is enabled", ext,
		         VK_KHR_DISPLAY_EXTENSION_NAME);
	}
#endif
	return false;
}

static bool
vk_is_instance_ext_supported(VkExtensionProperties *props, uint32_t prop_count, const char *ext)
{
	for (uint32_t j = 0; j < prop_count; j++) {
		if (strcmp(ext, props[j].extensionName) == 0) {
			return true;
		}
	}
	return false;
}

struct u_string_list *
vk_build_instance_extensions(struct vk_bundle *vk,
                             struct u_string_list *required_instance_ext_list,
                             struct u_string_list *optional_instance_ext_list)
{
	VkResult res;

	uint32_t prop_count = 0;
	res = vk->vkEnumerateInstanceExtensionProperties(NULL, &prop_count, NULL);
	vk_check_error("vkEnumerateInstanceExtensionProperties", res, NULL);

	VkExtensionProperties *props = U_TYPED_ARRAY_CALLOC(VkExtensionProperties, prop_count);
	res = vk->vkEnumerateInstanceExtensionProperties(NULL, &prop_count, props);
	vk_check_error_with_free("vkEnumerateInstanceExtensionProperties", res, NULL, props);

	struct u_string_list *ret = u_string_list_create_from_list(required_instance_ext_list);

	uint32_t optional_instance_ext_count = u_string_list_get_size(optional_instance_ext_list);
	const char *const *optional_instance_exts = u_string_list_get_data(optional_instance_ext_list);
	for (uint32_t i = 0; i < optional_instance_ext_count; i++) {
		const char *optional_ext = optional_instance_exts[i];

		if (vk_should_skip_optional_instance_ext(vk, required_instance_ext_list, optional_instance_ext_list,
		                                         optional_ext)) {
			continue;
		}

		if (!vk_is_instance_ext_supported(props, prop_count, optional_ext)) {
			VK_DEBUG(vk, "Optional instance extension %s not enabled, unsupported", optional_ext);
			continue;
		}

		int added = u_string_list_append_unique(ret, optional_ext);
		if (added == 1) {
			VK_DEBUG(vk, "Using optional instance ext %s", optional_ext);
		} else {
			VK_WARN(vk, "Duplicate instance extension %s not added twice", optional_ext);
		}
		break;
	}

	free(props);
	return ret;
}

static VkResult
vk_get_device_ext_props(struct vk_bundle *vk,
                        VkPhysicalDevice physical_device,
                        VkExtensionProperties **out_props,
                        uint32_t *out_prop_count)
{
	uint32_t prop_count = 0;
	VkResult res = vk->vkEnumerateDeviceExtensionProperties(physical_device, NULL, &prop_count, NULL);
	vk_check_error("vkEnumerateDeviceExtensionProperties", res, false);

	VkExtensionProperties *props = U_TYPED_ARRAY_CALLOC(VkExtensionProperties, prop_count);

	res = vk->vkEnumerateDeviceExtensionProperties(physical_device, NULL, &prop_count, props);
	vk_check_error_with_free("vkEnumerateDeviceExtensionProperties", res, false, props);

	// Check above returns on failure.
	*out_props = props;
	*out_prop_count = prop_count;

	return VK_SUCCESS;
}

static bool
vk_should_skip_optional_device_ext(struct vk_bundle *vk,
                                   struct u_string_list *required_device_ext_list,
                                   struct u_string_list *optional_device_ext_listconst,
                                   const char *ext)
{
#ifdef VK_EXT_display_control
	// only enable VK_EXT_display_control when we enabled VK_EXT_display_surface_counter instance ext
	if (strcmp(ext, VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME) == 0) {
		if (!vk->has_EXT_display_surface_counter) {
			VK_DEBUG(vk, "Skipping optional instance extension %s because %s instance ext is not enabled",
			         ext, VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME);
			return true;
		}
		VK_DEBUG(vk, "Not skipping optional instance extension %s because %s instance ext is enabled", ext,
		         VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME);
	}
#endif
	return false;
}

static bool
vk_build_device_extensions(struct vk_bundle *vk,
                           VkPhysicalDevice physical_device,
                           struct u_string_list *required_device_ext_list,
                           struct u_string_list *optional_device_ext_list,
                           struct u_string_list **out_device_ext_list)
{
	VkExtensionProperties *props = NULL;
	uint32_t prop_count = 0;
	if (vk_get_device_ext_props(vk, physical_device, &props, &prop_count) != VK_SUCCESS) {
		return false;
	}

	uint32_t required_device_ext_count = u_string_list_get_size(required_device_ext_list);
	const char *const *required_device_exts = u_string_list_get_data(required_device_ext_list);

	// error out if we don't support one of the required extensions
	for (uint32_t i = 0; i < required_device_ext_count; i++) {
		const char *ext = required_device_exts[i];
		if (!vk_check_extension(vk, props, prop_count, ext)) {
			VK_DEBUG(vk, "VkPhysicalDevice does not support required extension %s", ext);
			free(props);
			return false;
		}
		VK_DEBUG(vk, "Using required device ext %s", ext);
	}


	*out_device_ext_list = u_string_list_create_from_list(required_device_ext_list);


	uint32_t optional_device_ext_count = u_string_list_get_size(optional_device_ext_list);
	const char *const *optional_device_exts = u_string_list_get_data(optional_device_ext_list);

	for (uint32_t i = 0; i < optional_device_ext_count; i++) {
		const char *ext = optional_device_exts[i];

		if (vk_should_skip_optional_device_ext(vk, required_device_ext_list, optional_device_ext_list, ext)) {
			continue;
		}

		if (vk_check_extension(vk, props, prop_count, ext)) {
			VK_DEBUG(vk, "Using optional device ext %s", ext);
			int added = u_string_list_append_unique(*out_device_ext_list, ext);
			if (added == 0) {
				VK_WARN(vk, "Duplicate device extension %s not added twice", ext);
			}
		} else {
			VK_DEBUG(vk, "NOT using optional device ext %s", ext);
			continue;
		}
	}

	// Fill this out here.
	fill_in_has_device_extensions(vk, *out_device_ext_list);

	free(props);


	return true;
}

static inline void
append_to_pnext_chain(VkBaseInStructure *head, VkBaseInStructure *new_struct)
{
	assert(new_struct->pNext == NULL);
	// Insert ourselves between head and its previous pNext
	new_struct->pNext = head->pNext;
	head->pNext = (void *)new_struct;
}

/**
 * @brief Sets fields in @p device_features to true if and only if they are available and they are true in @p
 * optional_device_features (indicating a desire for that feature)
 *
 * @param vk self
 * @param physical_device The physical device to query
 * @param[in] optional_device_features The features to request if available
 * @param[out] device_features Populated with the subset of @p optional_device_features that are actually available.
 */
static void
filter_device_features(struct vk_bundle *vk,
                       VkPhysicalDevice physical_device,
                       const struct vk_device_features *optional_device_features,
                       struct vk_device_features *device_features)
{
	// If no features are requested, then noop.
	if (optional_device_features == NULL) {
		return;
	}

	/*
	 * The structs
	 */

#ifdef VK_EXT_robustness2
	VkPhysicalDeviceRobustness2FeaturesEXT robust_info = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
	    .pNext = NULL,
	};
#endif

#ifdef VK_KHR_timeline_semaphore
	VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timeline_semaphore_info = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR,
	    .pNext = NULL,
	};
#endif

	VkPhysicalDeviceFeatures2 physical_device_features = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
	    .pNext = NULL,
	};

#ifdef VK_EXT_robustness2
	if (vk->has_EXT_robustness2) {
		append_to_pnext_chain((VkBaseInStructure *)&physical_device_features,
		                      (VkBaseInStructure *)&robust_info);
	}
#endif

#ifdef VK_KHR_timeline_semaphore
	if (vk->has_KHR_timeline_semaphore) {
		append_to_pnext_chain((VkBaseInStructure *)&physical_device_features,
		                      (VkBaseInStructure *)&timeline_semaphore_info);
	}
#endif

	vk->vkGetPhysicalDeviceFeatures2( //
	    physical_device,              // physicalDevice
	    &physical_device_features);   // pFeatures


	/*
	 * Collect and transfer.
	 */

#define CHECK(feature, DEV_FEATURE) device_features->feature = optional_device_features->feature && (DEV_FEATURE)

#ifdef VK_EXT_robustness2
	CHECK(null_descriptor, robust_info.nullDescriptor);
#endif

#ifdef VK_KHR_timeline_semaphore
	CHECK(timeline_semaphore, timeline_semaphore_info.timelineSemaphore);
#endif
	CHECK(shader_storage_image_write_without_format,
	      physical_device_features.features.shaderStorageImageWriteWithoutFormat);

#undef CHECK


	VK_DEBUG(vk,
	         "Features:"
	         "\n\tnull_descriptor: %i"
	         "\n\tshader_storage_image_write_without_format: %i"
	         "\n\ttimeline_semaphore: %i",                               //
	         device_features->null_descriptor,                           //
	         device_features->shader_storage_image_write_without_format, //
	         device_features->timeline_semaphore);
}

VkResult
vk_create_device(struct vk_bundle *vk,
                 int forced_index,
                 bool only_compute,
                 VkQueueGlobalPriorityEXT global_priority,
                 struct u_string_list *required_device_ext_list,
                 struct u_string_list *optional_device_ext_list,
                 const struct vk_device_features *optional_device_features)
{
	VkResult ret;

	ret = vk_select_physical_device(vk, forced_index);
	if (ret != VK_SUCCESS) {
		return ret;
	}

	struct u_string_list *device_ext_list = NULL;
	if (!vk_build_device_extensions(vk, vk->physical_device, required_device_ext_list, optional_device_ext_list,
	                                &device_ext_list)) {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}


	/*
	 * Features
	 */

	struct vk_device_features device_features = {0};
	filter_device_features(vk, vk->physical_device, optional_device_features, &device_features);
	vk->features.timeline_semaphore = device_features.timeline_semaphore;

	/*
	 * Queue
	 */

	if (only_compute) {
		ret = vk_find_compute_only_queue(vk, &vk->queue_family_index);
	} else {
		ret = vk_find_graphics_queue(vk, &vk->queue_family_index);
	}

	if (ret != VK_SUCCESS) {
		return ret;
	}

	VkDeviceQueueGlobalPriorityCreateInfoEXT priority_info = {
	    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_EXT,
	    .pNext = NULL,
	    .globalPriority = global_priority,
	};

	float queue_priority = 0.0f;
	VkDeviceQueueCreateInfo queue_create_info = {
	    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
	    .pNext = NULL,
	    .queueCount = 1,
	    .queueFamilyIndex = vk->queue_family_index,
	    .pQueuePriorities = &queue_priority,
	};

	if (vk->has_EXT_global_priority) {
		priority_info.pNext = queue_create_info.pNext;
		queue_create_info.pNext = (void *)&priority_info;
	}


	/*
	 * Device
	 */

#ifdef VK_EXT_robustness2
	VkPhysicalDeviceRobustness2FeaturesEXT robust_info = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
	    .pNext = NULL,
	    .nullDescriptor = device_features.null_descriptor,
	};
#endif

#ifdef VK_KHR_timeline_semaphore
	VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timeline_semaphore_info = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR,
	    .pNext = NULL,
	    .timelineSemaphore = device_features.timeline_semaphore,
	};
#endif

	VkPhysicalDeviceFeatures enabled_features = {
	    .shaderStorageImageWriteWithoutFormat = device_features.shader_storage_image_write_without_format,
	};

	VkDeviceCreateInfo device_create_info = {
	    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
	    .queueCreateInfoCount = 1,
	    .pQueueCreateInfos = &queue_create_info,
	    .enabledExtensionCount = u_string_list_get_size(device_ext_list),
	    .ppEnabledExtensionNames = u_string_list_get_data(device_ext_list),
	    .pEnabledFeatures = &enabled_features,
	};

#ifdef VK_EXT_robustness2
	if (vk->has_EXT_robustness2) {
		append_to_pnext_chain((VkBaseInStructure *)&device_create_info, (VkBaseInStructure *)&robust_info);
	}
#endif

#ifdef VK_KHR_timeline_semaphore
	if (vk->has_KHR_timeline_semaphore) {
		append_to_pnext_chain((VkBaseInStructure *)&device_create_info,
		                      (VkBaseInStructure *)&timeline_semaphore_info);
	}
#endif

	ret = vk->vkCreateDevice(vk->physical_device, &device_create_info, NULL, &vk->device);

	u_string_list_destroy(&device_ext_list);

	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "vkCreateDevice: %s (%d)", vk_result_string(ret), ret);
		if (ret == VK_ERROR_NOT_PERMITTED_EXT) {
			VK_DEBUG(vk, "Is CAP_SYS_NICE set? Try: sudo setcap cap_sys_nice+ep monado-service");
		}
		return ret;
	}

	// We fill in these here as we want to be sure we have selected the physical device fully.
	fill_in_external_object_properties(vk);

	// Now setup all of the device specific functions.
	ret = vk_get_device_functions(vk);
	if (ret != VK_SUCCESS) {
		goto err_destroy;
	}
	vk->vkGetDeviceQueue(vk->device, vk->queue_family_index, 0, &vk->queue);

	return ret;

err_destroy:
	vk->vkDestroyDevice(vk->device, NULL);
	vk->device = NULL;

	return ret;
}

VkResult
vk_init_mutex(struct vk_bundle *vk)
{
	if (os_mutex_init(&vk->cmd_pool_mutex) < 0) {
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	if (os_mutex_init(&vk->queue_mutex) < 0) {
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	return VK_SUCCESS;
}

VkResult
vk_deinit_mutex(struct vk_bundle *vk)
{
	os_mutex_destroy(&vk->cmd_pool_mutex);
	os_mutex_destroy(&vk->queue_mutex);
	return VK_SUCCESS;
}

VkResult
vk_init_from_given(struct vk_bundle *vk,
                   PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr,
                   VkInstance instance,
                   VkPhysicalDevice physical_device,
                   VkDevice device,
                   uint32_t queue_family_index,
                   uint32_t queue_index,
                   bool timeline_semaphore_enabled,
                   enum u_logging_level log_level)
{
	VkResult ret;

	// First memset it clear.
	U_ZERO(vk);
	vk->log_level = log_level;

	ret = vk_get_loader_functions(vk, vkGetInstanceProcAddr);
	if (ret != VK_SUCCESS) {
		goto err_memset;
	}

	vk->instance = instance;
	vk->physical_device = physical_device;
	vk->device = device;
	vk->queue_family_index = queue_family_index;
	vk->queue_index = queue_index;

	// Fill in all instance functions.
	ret = vk_get_instance_functions(vk);
	if (ret != VK_SUCCESS) {
		goto err_memset;
	}

	// Fill out the device memory props here, as we are
	// passed a vulkan context and do not call selectPhysicalDevice()
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical_device, &vk->device_memory_props);


#ifdef VK_KHR_timeline_semaphore
	/*
	 * Has the timeline semaphore extension and feature been enabled?
	 * Need to do this before fill_in_external_object_properties.
	 */
	if (timeline_semaphore_enabled) {
		vk->has_KHR_timeline_semaphore = true;
		vk->features.timeline_semaphore = true;
	}
#endif

	// Fill in external object properties.
	fill_in_external_object_properties(vk);

	// Fill in all device functions.
	ret = vk_get_device_functions(vk);
	if (ret != VK_SUCCESS) {
		goto err_memset;
	}

	vk->vkGetDeviceQueue(vk->device, vk->queue_family_index, vk->queue_index, &vk->queue);

	// Create the pool.
	ret = vk_init_cmd_pool(vk);
	if (ret != VK_SUCCESS) {
		goto err_memset;
	}


	return VK_SUCCESS;

err_memset:
	U_ZERO(vk);
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
