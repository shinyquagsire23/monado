// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helper for getting information from a VkSurfaceKHR.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_vk
 */

#include "util/u_pretty_print.h"
#include "vk_surface_info.h"


/*
 *
 * Helpers.
 *
 */

#define P(...) u_pp(dg, __VA_ARGS__)
#define PNT(...) u_pp(dg, "\n\t" __VA_ARGS__)
#define PNTT(...) u_pp(dg, "\n\t\t" __VA_ARGS__)

XRT_CHECK_RESULT static VkResult
surface_info_get_present_modes(struct vk_bundle *vk, struct vk_surface_info *info, VkSurfaceKHR surface)
{
	VkResult ret;

	assert(info->present_modes == NULL);
	assert(info->present_mode_count == 0);

	ret = vk->vkGetPhysicalDeviceSurfacePresentModesKHR( //
	    vk->physical_device,                             //
	    surface,                                         //
	    &info->present_mode_count,                       //
	    NULL);                                           //
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkGetPhysicalDeviceSurfacePresentModesKHR: %s", vk_result_string(ret));
		return ret;
	}
	// Nothing to do.
	if (info->present_mode_count == 0) {
		return VK_SUCCESS;
	}

	info->present_modes = U_TYPED_ARRAY_CALLOC(VkPresentModeKHR, info->present_mode_count);
	ret = vk->vkGetPhysicalDeviceSurfacePresentModesKHR( //
	    vk->physical_device,                             //
	    surface,                                         //
	    &info->present_mode_count,                       //
	    info->present_modes);                            //
	if (ret == VK_SUCCESS) {
		return ret;
	}

	free(info->present_modes);
	info->present_mode_count = 0;
	info->present_modes = NULL;

	VK_ERROR(vk, "vkGetPhysicalDeviceSurfacePresentModesKHR: %s", vk_result_string(ret));

	return ret;
}

XRT_CHECK_RESULT static VkResult
surface_info_get_surface_formats(struct vk_bundle *vk, struct vk_surface_info *info, VkSurfaceKHR surface)
{
	VkResult ret;

	assert(info->formats == NULL);
	assert(info->format_count == 0);

	ret = vk->vkGetPhysicalDeviceSurfaceFormatsKHR( //
	    vk->physical_device,                        //
	    surface,                                    //
	    &info->format_count,                        //
	    NULL);                                      //
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkGetPhysicalDeviceSurfaceFormatsKHR: %s", vk_result_string(ret));
		return ret;
	}
	// Nothing to do.
	if (info->format_count == 0) {
		return VK_SUCCESS;
	}

	info->formats = U_TYPED_ARRAY_CALLOC(VkSurfaceFormatKHR, info->format_count);
	ret = vk->vkGetPhysicalDeviceSurfaceFormatsKHR( //
	    vk->physical_device,                        //
	    surface,                                    //
	    &info->format_count,                        //
	    info->formats);                             //
	if (ret == VK_SUCCESS) {
		return ret;
	}

	free(info->formats);
	info->format_count = 0;
	info->formats = NULL;

	VK_ERROR(vk, "vkGetPhysicalDeviceSurfaceFormatsKHR: %s", vk_result_string(ret));

	return ret;
}


/*
 *
 * 'Exported' functions.
 *
 */

void
vk_surface_info_destroy(struct vk_surface_info *info)
{
	if (info->present_modes != NULL) {
		free(info->present_modes);
		info->present_mode_count = 0;
		info->present_modes = NULL;
	}

	if (info->formats != NULL) {
		free(info->formats);
		info->format_count = 0;
		info->formats = NULL;
	}

	U_ZERO(info);
}

XRT_CHECK_RESULT VkResult
vk_surface_info_fill_in(struct vk_bundle *vk, struct vk_surface_info *info, VkSurfaceKHR surface)
{
	VkResult ret;

	ret = surface_info_get_present_modes(vk, info, surface);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "surface_info_get_present_modes: %s", vk_result_string(ret));
		goto error;
	}

	ret = surface_info_get_surface_formats(vk, info, surface);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "surface_info_get_surface_formats: %s", vk_result_string(ret));
		goto error;
	}

	ret = vk->vkGetPhysicalDeviceSurfaceCapabilitiesKHR( //
	    vk->physical_device,                             //
	    surface,                                         //
	    &info->caps);                                    //
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR: %s", vk_result_string(ret));
		goto error;
	}

#ifdef VK_EXT_display_surface_counter
	if (vk->has_EXT_display_control) {
		info->caps2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_EXT;
		ret = vk->vkGetPhysicalDeviceSurfaceCapabilities2EXT( //
		    vk->physical_device,                              //
		    surface,                                          //
		    &info->caps2);                                    //
		if (ret != VK_SUCCESS) {
			VK_ERROR(vk, "vkGetPhysicalDeviceSurfaceCapabilities2EXT: %s", vk_result_string(ret));
			goto error;
		}
	}
#endif

	return VK_SUCCESS;

error:
	vk_surface_info_destroy(info);

	return ret;
}

void
vk_print_surface_info(struct vk_bundle *vk, struct vk_surface_info *info, enum u_logging_level log_level)
{
	if (vk->log_level > log_level) {
		return;
	}

	struct u_pp_sink_stack_only sink;
	u_pp_delegate_t dg = u_pp_sink_stack_only_init(&sink);

	P("VkSurfaceKHR info:");
	PNT("caps.minImageCount: %u", info->caps.minImageCount);
	PNT("caps.maxImageCount: %u", info->caps.maxImageCount);
	PNT("caps.currentExtent: %ux%u", info->caps.currentExtent.width, info->caps.currentExtent.height);
	PNT("caps.minImageExtent: %ux%u", info->caps.minImageExtent.width, info->caps.minImageExtent.height);
	PNT("caps.maxImageExtent: %ux%u", info->caps.maxImageExtent.width, info->caps.maxImageExtent.height);
	PNT("caps.maxImageArrayLayers: %u", info->caps.maxImageArrayLayers);
	// PNT("caps.supportedTransforms")
	// PNT("caps.currentTransform")
	// PNT("caps.supportedCompositeAlpha")
	// PNT("caps.supportedUsageFlags")

	PNT("present_modes(%u):", info->present_mode_count);
	for (uint32_t i = 0; i < info->present_mode_count; i++) {
		PNTT("%s", vk_present_mode_string(info->present_modes[i]));
	}

	PNT("formats(%u):", info->format_count);
	for (uint32_t i = 0; i < info->format_count; i++) {
		VkSurfaceFormatKHR *f = &info->formats[i];
		PNTT("[format = %s, colorSpace = %s]", vk_format_string(f->format),
		     vk_color_space_string(f->colorSpace));
	}

	U_LOG_IFL(log_level, vk->log_level, "%s", sink.buffer);
}
