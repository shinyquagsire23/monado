// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common Vulkan code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
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
 * Functions.
 *
 */

bool
vk_get_memory_type(struct vk_bundle *vk, uint32_t type_bits, VkMemoryPropertyFlags memory_props, uint32_t *out_type_id)
{

	for (uint32_t i = 0; i < vk->device_memory_props.memoryTypeCount; i++) {
		uint32_t propertyFlags = vk->device_memory_props.memoryTypes[i].propertyFlags;
		if ((type_bits & 1) == 1) {
			if ((propertyFlags & memory_props) == memory_props) {
				*out_type_id = i;
				return true;
			}
		}
		type_bits >>= 1;
	}

	VK_DEBUG(vk, "Could not find memory type!");

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
		VK_ERROR(vk,
		         "client_vk_swapchain - Got too little memory "
		         "%u vs %u\n",
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
vk_create_image_from_native(struct vk_bundle *vk,
                            const struct xrt_swapchain_create_info *info,
                            struct xrt_image_native *image_native,
                            VkImage *out_image,
                            VkDeviceMemory *out_mem)
{
	VkImageUsageFlags image_usage = vk_swapchain_usage_flags(vk, (VkFormat)info->format, info->bits);
	if (image_usage == 0) {
		U_LOG_E(
		    "vk_create_image_from_native: Unsupported swapchain usage "
		    "flags");
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

	VkImageCreateInfo vk_info = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	    .pNext = &external_memory_image_create_info,
	    .imageType = VK_IMAGE_TYPE_2D,
	    .format = (VkFormat)info->format,
	    .extent = {.width = info->width, .height = info->height, .depth = 1},
	    .mipLevels = info->mip_count,
	    .arrayLayers = info->array_size,
	    .samples = VK_SAMPLE_COUNT_1_BIT,
	    .tiling = VK_IMAGE_TILING_OPTIMAL,
	    .usage = image_usage,
	    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

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
vk_create_semaphore_from_native(struct vk_bundle *vk, xrt_graphics_sync_handle_t native, VkSemaphore *out_sem)
{
	VkResult ret;

	VkSemaphoreCreateInfo semaphore_create_info = {
	    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	ret = vk->vkCreateSemaphore(vk->device, &semaphore_create_info, NULL, out_sem);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateSemaphore: %s", vk_result_string(ret));
		// Nothing to cleanup
		return ret;
	}
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
	VkImportSemaphoreFdInfoKHR import_semaphore_fd_info = {
	    .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
	    .semaphore = *out_sem,
	    .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
	    .fd = native,
	};
	ret = vk->vkImportSemaphoreFdKHR(vk->device, &import_semaphore_fd_info);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkImportSemaphoreFdKHR: %s", vk_result_string(ret));
		vk->vkDestroySemaphore(vk->device, *out_sem, NULL);
		return ret;
	}
#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
	VkImportSemaphoreWin32HandleInfoKHR import_semaphore_handle_info = {
	    .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR,
	    .semaphore = *out_sem,
	    .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT,
	    .handle = native,
	};
	ret = vk->vkImportSemaphoreWin32HandleKHR(vk->device, &import_semaphore_handle_info);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkImportSemaphoreWin32HandleKHR: %s", vk_result_string(ret));
		vk->vkDestroySemaphore(vk->device, *out_sem, NULL);
		return ret;
	}
#else
#error "Not implemented for this underlying handle type!"
#endif
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
	// clang-format on

	return VK_SUCCESS;
}

VkResult
vk_get_instance_functions(struct vk_bundle *vk)
{
	// clang-format off
	vk->vkDestroyInstance                         = GET_INS_PROC(vk, vkDestroyInstance);
	vk->vkGetDeviceProcAddr                       = GET_INS_PROC(vk, vkGetDeviceProcAddr);
	vk->vkCreateDevice                            = GET_INS_PROC(vk, vkCreateDevice);
	vk->vkEnumeratePhysicalDevices                = GET_INS_PROC(vk, vkEnumeratePhysicalDevices);
	vk->vkGetPhysicalDeviceProperties             = GET_INS_PROC(vk, vkGetPhysicalDeviceProperties);
	vk->vkGetPhysicalDeviceProperties2            = GET_INS_PROC(vk, vkGetPhysicalDeviceProperties2);
	vk->vkGetPhysicalDeviceMemoryProperties       = GET_INS_PROC(vk, vkGetPhysicalDeviceMemoryProperties);
	vk->vkGetPhysicalDeviceQueueFamilyProperties  = GET_INS_PROC(vk, vkGetPhysicalDeviceQueueFamilyProperties);
	vk->vkCreateDebugReportCallbackEXT            = GET_INS_PROC(vk, vkCreateDebugReportCallbackEXT);
	vk->vkDestroyDebugReportCallbackEXT           = GET_INS_PROC(vk, vkDestroyDebugReportCallbackEXT);
	vk->vkDestroySurfaceKHR                       = GET_INS_PROC(vk, vkDestroySurfaceKHR);
	vk->vkGetPhysicalDeviceSurfaceCapabilitiesKHR = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
	vk->vkGetPhysicalDeviceSurfaceFormatsKHR      = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfaceFormatsKHR);
	vk->vkGetPhysicalDeviceSurfacePresentModesKHR = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfacePresentModesKHR);
	vk->vkGetPhysicalDeviceSurfaceSupportKHR      = GET_INS_PROC(vk, vkGetPhysicalDeviceSurfaceSupportKHR);
	vk->vkGetPhysicalDeviceFormatProperties       = GET_INS_PROC(vk, vkGetPhysicalDeviceFormatProperties);
	vk->vkEnumerateDeviceExtensionProperties      = GET_INS_PROC(vk, vkEnumerateDeviceExtensionProperties);

#ifdef VK_USE_PLATFORM_XCB_KHR
	vk->vkCreateXcbSurfaceKHR = GET_INS_PROC(vk, vkCreateXcbSurfaceKHR);
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	vk->vkCreateWaylandSurfaceKHR = GET_INS_PROC(vk, vkCreateWaylandSurfaceKHR);
#endif

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
	vk->vkCreateDisplayPlaneSurfaceKHR               = GET_INS_PROC(vk, vkCreateDisplayPlaneSurfaceKHR);
	vk->vkGetDisplayPlaneCapabilitiesKHR             = GET_INS_PROC(vk, vkGetDisplayPlaneCapabilitiesKHR);
	vk->vkGetPhysicalDeviceDisplayPropertiesKHR      = GET_INS_PROC(vk, vkGetPhysicalDeviceDisplayPropertiesKHR);
	vk->vkGetPhysicalDeviceDisplayPlanePropertiesKHR = GET_INS_PROC(vk, vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
	vk->vkGetDisplayModePropertiesKHR                = GET_INS_PROC(vk, vkGetDisplayModePropertiesKHR);
	vk->vkAcquireXlibDisplayEXT                      = GET_INS_PROC(vk, vkAcquireXlibDisplayEXT);
	vk->vkReleaseDisplayEXT                          = GET_INS_PROC(vk, vkReleaseDisplayEXT);
	vk->vkGetRandROutputDisplayEXT                   = GET_INS_PROC(vk, vkGetRandROutputDisplayEXT);
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
	vk->vkCreateAndroidSurfaceKHR                    = GET_INS_PROC(vk, vkCreateAndroidSurfaceKHR);
#endif

#ifdef VK_USE_PLATFORM_WIN32_KHR
	vk->vkCreateWin32SurfaceKHR                      = GET_INS_PROC(vk, vkCreateWin32SurfaceKHR);
#endif
	// clang-format on

	return VK_SUCCESS;
}

static VkResult
vk_get_device_functions(struct vk_bundle *vk)
{
	// clang-format off
	vk->vkDestroyDevice               = GET_DEV_PROC(vk, vkDestroyDevice);
	vk->vkDeviceWaitIdle              = GET_DEV_PROC(vk, vkDeviceWaitIdle);
	vk->vkAllocateMemory              = GET_DEV_PROC(vk, vkAllocateMemory);
	vk->vkFreeMemory                  = GET_DEV_PROC(vk, vkFreeMemory);
	vk->vkMapMemory                   = GET_DEV_PROC(vk, vkMapMemory);
	vk->vkUnmapMemory                 = GET_DEV_PROC(vk, vkUnmapMemory);
	vk->vkGetMemoryFdKHR              = GET_DEV_PROC(vk, vkGetMemoryFdKHR);
#ifdef VK_USE_PLATFORM_ANDROID_KHR
	vk->vkGetMemoryAndroidHardwareBufferANDROID = GET_DEV_PROC(vk, vkGetMemoryAndroidHardwareBufferANDROID);
	vk->vkGetAndroidHardwareBufferPropertiesANDROID = GET_DEV_PROC(vk, vkGetAndroidHardwareBufferPropertiesANDROID);
#endif

#ifdef VK_USE_PLATFORM_WIN32_KHR
	vk->vkGetMemoryWin32HandleKHR     = GET_DEV_PROC(vk, vkGetMemoryWin32HandleKHR);
#endif

	vk->vkCreateBuffer                = GET_DEV_PROC(vk, vkCreateBuffer);
	vk->vkDestroyBuffer               = GET_DEV_PROC(vk, vkDestroyBuffer);
	vk->vkBindBufferMemory            = GET_DEV_PROC(vk, vkBindBufferMemory);
	vk->vkGetBufferMemoryRequirements = GET_DEV_PROC(vk, vkGetBufferMemoryRequirements);
	vk->vkFlushMappedMemoryRanges     = GET_DEV_PROC(vk, vkFlushMappedMemoryRanges);
	vk->vkCreateImage                 = GET_DEV_PROC(vk, vkCreateImage);
	vk->vkGetImageMemoryRequirements  = GET_DEV_PROC(vk, vkGetImageMemoryRequirements);
	vk->vkBindImageMemory             = GET_DEV_PROC(vk, vkBindImageMemory);
	vk->vkDestroyImage                = GET_DEV_PROC(vk, vkDestroyImage);
	vk->vkCreateImageView             = GET_DEV_PROC(vk, vkCreateImageView);
	vk->vkDestroyImageView            = GET_DEV_PROC(vk, vkDestroyImageView);
	vk->vkCreateSampler               = GET_DEV_PROC(vk, vkCreateSampler);
	vk->vkDestroySampler              = GET_DEV_PROC(vk, vkDestroySampler);
	vk->vkCreateShaderModule          = GET_DEV_PROC(vk, vkCreateShaderModule);
	vk->vkDestroyShaderModule         = GET_DEV_PROC(vk, vkDestroyShaderModule);
	vk->vkCreateCommandPool           = GET_DEV_PROC(vk, vkCreateCommandPool);
	vk->vkDestroyCommandPool          = GET_DEV_PROC(vk, vkDestroyCommandPool);
	vk->vkAllocateCommandBuffers      = GET_DEV_PROC(vk, vkAllocateCommandBuffers);
	vk->vkBeginCommandBuffer          = GET_DEV_PROC(vk, vkBeginCommandBuffer);
	vk->vkCmdPipelineBarrier          = GET_DEV_PROC(vk, vkCmdPipelineBarrier);
	vk->vkCmdBeginRenderPass          = GET_DEV_PROC(vk, vkCmdBeginRenderPass);
	vk->vkCmdSetScissor               = GET_DEV_PROC(vk, vkCmdSetScissor);
	vk->vkCmdSetViewport              = GET_DEV_PROC(vk, vkCmdSetViewport);
	vk->vkCmdClearColorImage          = GET_DEV_PROC(vk, vkCmdClearColorImage);
	vk->vkCmdEndRenderPass            = GET_DEV_PROC(vk, vkCmdEndRenderPass);
	vk->vkCmdBindDescriptorSets       = GET_DEV_PROC(vk, vkCmdBindDescriptorSets);
	vk->vkCmdBindPipeline             = GET_DEV_PROC(vk, vkCmdBindPipeline);
	vk->vkCmdBindVertexBuffers        = GET_DEV_PROC(vk, vkCmdBindVertexBuffers);
	vk->vkCmdBindIndexBuffer          = GET_DEV_PROC(vk, vkCmdBindIndexBuffer);
	vk->vkCmdDraw                     = GET_DEV_PROC(vk, vkCmdDraw);
	vk->vkCmdDrawIndexed              = GET_DEV_PROC(vk, vkCmdDrawIndexed);
	vk->vkEndCommandBuffer            = GET_DEV_PROC(vk, vkEndCommandBuffer);
	vk->vkFreeCommandBuffers          = GET_DEV_PROC(vk, vkFreeCommandBuffers);
	vk->vkCreateRenderPass            = GET_DEV_PROC(vk, vkCreateRenderPass);
	vk->vkDestroyRenderPass           = GET_DEV_PROC(vk, vkDestroyRenderPass);
	vk->vkCreateFramebuffer           = GET_DEV_PROC(vk, vkCreateFramebuffer);
	vk->vkDestroyFramebuffer          = GET_DEV_PROC(vk, vkDestroyFramebuffer);
	vk->vkCreatePipelineCache         = GET_DEV_PROC(vk, vkCreatePipelineCache);
	vk->vkDestroyPipelineCache        = GET_DEV_PROC(vk, vkDestroyPipelineCache);
	vk->vkCreateDescriptorPool        = GET_DEV_PROC(vk, vkCreateDescriptorPool);
	vk->vkDestroyDescriptorPool       = GET_DEV_PROC(vk, vkDestroyDescriptorPool);
	vk->vkAllocateDescriptorSets      = GET_DEV_PROC(vk, vkAllocateDescriptorSets);
	vk->vkFreeDescriptorSets          = GET_DEV_PROC(vk, vkFreeDescriptorSets);
	vk->vkCreateGraphicsPipelines     = GET_DEV_PROC(vk, vkCreateGraphicsPipelines);
	vk->vkDestroyPipeline             = GET_DEV_PROC(vk, vkDestroyPipeline);
	vk->vkCreatePipelineLayout        = GET_DEV_PROC(vk, vkCreatePipelineLayout);
	vk->vkDestroyPipelineLayout       = GET_DEV_PROC(vk, vkDestroyPipelineLayout);
	vk->vkCreateDescriptorSetLayout   = GET_DEV_PROC(vk, vkCreateDescriptorSetLayout);
	vk->vkUpdateDescriptorSets        = GET_DEV_PROC(vk, vkUpdateDescriptorSets);
	vk->vkDestroyDescriptorSetLayout  = GET_DEV_PROC(vk, vkDestroyDescriptorSetLayout);
	vk->vkGetDeviceQueue              = GET_DEV_PROC(vk, vkGetDeviceQueue);
	vk->vkQueueSubmit                 = GET_DEV_PROC(vk, vkQueueSubmit);
	vk->vkQueueWaitIdle               = GET_DEV_PROC(vk, vkQueueWaitIdle);
	vk->vkCreateSemaphore             = GET_DEV_PROC(vk, vkCreateSemaphore);
	vk->vkDestroySemaphore            = GET_DEV_PROC(vk, vkDestroySemaphore);
	vk->vkCreateFence                 = GET_DEV_PROC(vk, vkCreateFence);
	vk->vkWaitForFences               = GET_DEV_PROC(vk, vkWaitForFences);
	vk->vkDestroyFence                = GET_DEV_PROC(vk, vkDestroyFence);
	vk->vkResetFences                 = GET_DEV_PROC(vk, vkResetFences);
	vk->vkCreateSwapchainKHR          = GET_DEV_PROC(vk, vkCreateSwapchainKHR);
	vk->vkDestroySwapchainKHR         = GET_DEV_PROC(vk, vkDestroySwapchainKHR);
	vk->vkGetSwapchainImagesKHR       = GET_DEV_PROC(vk, vkGetSwapchainImagesKHR);
	vk->vkAcquireNextImageKHR         = GET_DEV_PROC(vk, vkAcquireNextImageKHR);
	vk->vkQueuePresentKHR             = GET_DEV_PROC(vk, vkQueuePresentKHR);

#ifdef VK_USE_PLATFORM_WIN32_KHR
	vk->vkImportSemaphoreWin32HandleKHR = GET_DEV_PROC(vk, vkImportSemaphoreWin32HandleKHR);
#else
	vk->vkImportSemaphoreFdKHR        = GET_DEV_PROC(vk, vkImportSemaphoreFdKHR);
	vk->vkGetSemaphoreFdKHR           = GET_DEV_PROC(vk, vkGetSemaphoreFdKHR);
#endif
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
			VK_ERROR(vk,
			         "Attempted to force GPU index %d, but only %d "
			         "GPUs are available",
			         forced_index, gpu_count);
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

	// Fill out the device memory props as well.
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical_device, &vk->device_memory_props);

	return VK_SUCCESS;
}

static VkResult
vk_find_graphics_queue(struct vk_bundle *vk, uint32_t *out_graphics_queue)
{
	/* Find the first graphics queue */
	uint32_t num_queues = 0;
	uint32_t i = 0;
	vk->vkGetPhysicalDeviceQueueFamilyProperties(vk->physical_device, &num_queues, NULL);

	VkQueueFamilyProperties *queue_family_props = U_TYPED_ARRAY_CALLOC(VkQueueFamilyProperties, num_queues);

	vk->vkGetPhysicalDeviceQueueFamilyProperties(vk->physical_device, &num_queues, queue_family_props);

	if (num_queues == 0) {
		VK_DEBUG(vk, "Failed to get queue properties");
		goto err_free;
	}

	for (i = 0; i < num_queues; i++) {
		if (queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			break;
		}
	}

	if (i >= num_queues) {
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

static bool
vk_check_extension(struct vk_bundle *vk, VkExtensionProperties *props, uint32_t num_props, const char *ext)
{
	for (uint32_t i = 0; i < num_props; i++) {
		if (strcmp(props[i].extensionName, ext) == 0) {

			if (strcmp(ext, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME) == 0) {
				vk->has_GOOGLE_display_timing = true;
			}

			return true;
		}
	}

	return false;
}

static bool
vk_get_device_ext_props(struct vk_bundle *vk,
                        VkPhysicalDevice physical_device,
                        VkExtensionProperties **props,
                        uint32_t *num_props)
{
	VkResult res = vk->vkEnumerateDeviceExtensionProperties(physical_device, NULL, num_props, NULL);
	vk_check_error("vkEnumerateDeviceExtensionProperties", res, false);

	*props = U_TYPED_ARRAY_CALLOC(VkExtensionProperties, *num_props);

	res = vk->vkEnumerateDeviceExtensionProperties(physical_device, NULL, num_props, *props);
	vk_check_error_with_free("vkEnumerateDeviceExtensionProperties", res, false, props);

	return true;
}

static bool
vk_build_device_extensions(struct vk_bundle *vk,
                           VkPhysicalDevice physical_device,
                           const char *const *required_device_extensions,
                           size_t num_required_device_extensions,
                           const char *const *optional_device_extensions,
                           size_t num_optional_device_extensions,
                           const char ***out_device_extensions,
                           size_t *out_num_device_extensions)
{
	VkExtensionProperties *props;
	uint32_t num_props;
	if (!vk_get_device_ext_props(vk, physical_device, &props, &num_props)) {
		return false;
	}

	int max_exts = num_required_device_extensions + num_optional_device_extensions;

	const char **device_extensions = U_TYPED_ARRAY_CALLOC(const char *, max_exts);

	for (uint32_t i = 0; i < num_required_device_extensions; i++) {
		const char *ext = required_device_extensions[i];
		if (!vk_check_extension(vk, props, num_props, ext)) {
			U_LOG_E(
			    "VkPhysicalDevice does not support required "
			    "extension %s",
			    ext);
			free(props);
			return false;
		}
		device_extensions[i] = ext;
	}

	uint32_t num_device_extensions = num_required_device_extensions;
	for (uint32_t i = 0; i < num_optional_device_extensions; i++) {
		const char *ext = optional_device_extensions[i];
		if (vk_check_extension(vk, props, num_props, ext)) {
			U_LOG_D("Using optional ext %s", ext);
			device_extensions[num_device_extensions++] = ext;
		} else {
			continue;
		}
	}

	free(props);

	*out_device_extensions = device_extensions;
	*out_num_device_extensions = num_device_extensions;

	return true;
}

VkResult
vk_create_device(struct vk_bundle *vk,
                 int forced_index,
                 const char *const *required_device_extensions,
                 size_t num_required_device_extensions,
                 const char *const *optional_device_extensions,
                 size_t num_optional_device_extensions)
{
	VkResult ret;

	ret = vk_select_physical_device(vk, forced_index);
	if (ret != VK_SUCCESS) {
		return ret;
	}

	const char **device_extensions;
	size_t num_device_extensions;
	if (!vk_build_device_extensions(vk, vk->physical_device, required_device_extensions,
	                                num_required_device_extensions, optional_device_extensions,
	                                num_optional_device_extensions, &device_extensions, &num_device_extensions)) {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}

	VkPhysicalDeviceFeatures *enabled_features = NULL;

	float queue_priority = 0.0f;
	VkDeviceQueueCreateInfo queue_create_info = {
	    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
	    .queueCount = 1,
	    .pQueuePriorities = &queue_priority,
	};

	ret = vk_find_graphics_queue(vk, &queue_create_info.queueFamilyIndex);
	if (ret != VK_SUCCESS) {
		return ret;
	}

	VkDeviceCreateInfo device_create_info = {
	    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
	    .queueCreateInfoCount = 1,
	    .pQueueCreateInfos = &queue_create_info,
	    .enabledExtensionCount = num_device_extensions,
	    .ppEnabledExtensionNames = device_extensions,
	    .pEnabledFeatures = enabled_features,
	};

	ret = vk->vkCreateDevice(vk->physical_device, &device_create_info, NULL, &vk->device);

	free(device_extensions);

	if (ret != VK_SUCCESS) {
		VK_DEBUG(vk, "vkCreateDevice: %s", vk_result_string(ret));
		return ret;
	}

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
vk_init_from_given(struct vk_bundle *vk,
                   PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr,
                   VkInstance instance,
                   VkPhysicalDevice physical_device,
                   VkDevice device,
                   uint32_t queue_family_index,
                   uint32_t queue_index)
{
	VkResult ret;

	// First memset it clear.
	U_ZERO(vk);

	vk->vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	vk->instance = instance;
	vk->physical_device = physical_device;
	vk->device = device;
	vk->queue_family_index = queue_family_index;
	vk->queue_index = queue_index;

	// Not really needed but just in case.
	vk->vkCreateInstance = GET_PROC(vk, vkCreateInstance);

	// Fill in all instance functions.
	ret = vk_get_instance_functions(vk);
	if (ret != VK_SUCCESS) {
		goto err_memset;
	}

	// Fill out the device memory props here, as we are
	// passed a vulkan context and do not call selectPhysicalDevice()
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical_device, &vk->device_memory_props);

	// Fill in all device functions.
	ret = vk_get_device_functions(vk);
	if (ret != VK_SUCCESS) {
		goto err_memset;
	}

	vk->vkGetDeviceQueue(vk->device, vk->queue_family_index, 0, &vk->queue);

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

VkAccessFlags
vk_swapchain_access_flags(enum xrt_swapchain_usage_bits bits)
{
	VkAccessFlags result = 0;
	if ((bits & XRT_SWAPCHAIN_USAGE_UNORDERED_ACCESS) != 0) {
		result |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		if ((bits & XRT_SWAPCHAIN_USAGE_COLOR) != 0) {
			result |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		}
		if ((bits & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0) {
			result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}
	}
	if ((bits & XRT_SWAPCHAIN_USAGE_COLOR) != 0) {
		result |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	}
	if ((bits & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0) {
		result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}
	if ((bits & XRT_SWAPCHAIN_USAGE_TRANSFER_SRC) != 0) {
		result |= VK_ACCESS_TRANSFER_READ_BIT;
	}
	if ((bits & XRT_SWAPCHAIN_USAGE_TRANSFER_DST) != 0) {
		result |= VK_ACCESS_TRANSFER_WRITE_BIT;
	}
	if ((bits & XRT_SWAPCHAIN_USAGE_SAMPLED) != 0) {
		result |= VK_ACCESS_SHADER_READ_BIT;
	}
	return result;
}

static bool
check_feature(VkFormat format,
              enum xrt_swapchain_usage_bits usage,
              VkFormatFeatureFlags format_features,
              VkFormatFeatureFlags flag)
{
	if ((format_features & flag) == 0) {
		U_LOG_E(
		    "vk_swapchain_usage_flags: %s requested but %s not "
		    "supported for format %s (%08x) (%08x)",
		    xrt_swapchain_usage_string(usage), vk_format_feature_string(flag), vk_color_format_string(format),
		    format_features, flag);
		return false;
	}
	return true;
}

VkImageUsageFlags
vk_swapchain_usage_flags(struct vk_bundle *vk, VkFormat format, enum xrt_swapchain_usage_bits bits)
{
	VkFormatProperties prop;
	vk->vkGetPhysicalDeviceFormatProperties(vk->physical_device, format, &prop);

	VkImageUsageFlags image_usage = 0;

	if ((bits & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0) {
		if (!check_feature(format, XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL, prop.optimalTilingFeatures,
		                   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
			return 0;
		}
		image_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	}

	if ((prop.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0) {
		image_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	}

	if ((bits & XRT_SWAPCHAIN_USAGE_COLOR) != 0) {
		if (!check_feature(format, XRT_SWAPCHAIN_USAGE_COLOR, prop.optimalTilingFeatures,
		                   VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
			return 0;
		}
		image_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if ((bits & XRT_SWAPCHAIN_USAGE_TRANSFER_SRC) != 0) {
		if (!check_feature(format, XRT_SWAPCHAIN_USAGE_TRANSFER_SRC, prop.optimalTilingFeatures,
		                   VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) {
			return 0;
		}
		image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if ((bits & XRT_SWAPCHAIN_USAGE_TRANSFER_DST) != 0) {
		if (!check_feature(format, XRT_SWAPCHAIN_USAGE_TRANSFER_DST, prop.optimalTilingFeatures,
		                   VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) {
			return 0;
		}
		image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}
	if ((bits & XRT_SWAPCHAIN_USAGE_SAMPLED) != 0) {
		if (!check_feature(format, XRT_SWAPCHAIN_USAGE_SAMPLED, prop.optimalTilingFeatures,
		                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
			return 0;
		}
		image_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	if ((bits & XRT_SWAPCHAIN_USAGE_INPUT_ATTACHMENT) != 0) {
		image_usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	}

	return image_usage;
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

	VkMemoryAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	                                   .allocationSize = requirements.size};

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
