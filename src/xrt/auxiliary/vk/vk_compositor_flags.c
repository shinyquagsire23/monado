// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Flags helpers for compositor swapchain images.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Benjamin Saunders <ben.e.saunders@gmail.com>
 * @ingroup aux_vk
 */

#include "xrt/xrt_handles.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "vk/vk_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 *
 * Helpers.
 *
 */

static bool
check_feature(VkFormat format,
              enum xrt_swapchain_usage_bits usage,
              VkFormatFeatureFlags format_features,
              VkFormatFeatureFlags flag)
{
	if ((format_features & flag) == 0) {
		U_LOG_E("vk_csci_get_image_usage_flags: %s requested but %s not supported for format %s (%08x) (%08x)",
		        xrt_swapchain_usage_string(usage), vk_format_feature_string(flag),
		        vk_color_format_string(format), format_features, flag);
		return false;
	}
	return true;
}


/*
 *
 * 'Exported' functions.
 *
 */

VkAccessFlags
vk_csci_get_barrier_access_mask(enum xrt_swapchain_usage_bits bits)
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

VkImageLayout
vk_csci_get_barrier_optimal_layout(VkFormat format)
{
	switch (format) {
	case VK_FORMAT_S8_UINT:
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT: //
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_R16G16B16_UNORM:
	case VK_FORMAT_R16G16B16_SFLOAT:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_R8G8B8_SRGB:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_B8G8R8_UNORM: //
		return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	default: //
		assert(false && !"Format not supported!");
	}
}

VkImageAspectFlags
vk_csci_get_barrier_aspect_mask(VkFormat format)
{
	switch (format) {
	case VK_FORMAT_S8_UINT: // Stencil only.
		return VK_IMAGE_ASPECT_STENCIL_BIT;
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D32_SFLOAT: // Depth only
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT: // Depth % stencil, barrier both.
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_R16G16B16_UNORM:
	case VK_FORMAT_R16G16B16_SFLOAT:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_R8G8B8_SRGB:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_B8G8R8_UNORM: // Color only.
		return VK_IMAGE_ASPECT_COLOR_BIT;
	default: //
		assert(false && !"Format not supported!");
	}
}

VkImageAspectFlags
vk_csci_get_image_view_aspect(VkFormat format, enum xrt_swapchain_usage_bits bits)
{
	switch (format) {
	case VK_FORMAT_S8_UINT: // Stencil only.
		return VK_IMAGE_ASPECT_STENCIL_BIT;
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D32_SFLOAT: // Depth only.
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT: // Depth & stencil, only want to sample depth.
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_R16G16B16_UNORM:
	case VK_FORMAT_R16G16B16_SFLOAT:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_R8G8B8_SRGB:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_B8G8R8_UNORM: // Color only.
		return VK_IMAGE_ASPECT_COLOR_BIT;
	default: //
		assert(false && !"Format not supported!");
		return 0;
	}
}

VkImageUsageFlags
vk_csci_get_image_usage_flags(struct vk_bundle *vk, VkFormat format, enum xrt_swapchain_usage_bits bits)
{
	VkFormatProperties prop;
	vk->vkGetPhysicalDeviceFormatProperties(vk->physical_device, format, &prop);

	VkImageUsageFlags image_usage = 0;

	if ((bits & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0) {
		if (!check_feature(format, XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL, prop.optimalTilingFeatures,
		                   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
			return 0;
		}
		image_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}

	if ((prop.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0) {
		image_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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

	// For compositors to be able to read it.
	if (true) {
		VkFormatFeatureFlags format_features = prop.optimalTilingFeatures;
		VkFormatFeatureFlags flag = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
		if ((format_features & flag) == 0) {
			U_LOG_E("%s: Compositor needs %s but not supported for format %s (%08x) (%08x)", __func__,
			        vk_format_feature_string(flag), vk_color_format_string(format), format_features, flag);
			return 0;
		}

		image_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}

	return image_usage;
}
