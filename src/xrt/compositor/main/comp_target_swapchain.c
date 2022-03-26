// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Target Vulkan swapchain code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include "xrt/xrt_config_os.h"

#include "util/u_misc.h"
#include "util/u_pacing.h"

#include "main/comp_compositor.h"
#include "main/comp_target_swapchain.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


/*
 *
 * Types, defines and data.
 *
 */

/*!
 * These formats will be 'preferred' - in future we may wish to give preference
 * to higher bit depths if they are available, but most display devices we are
 * interested in should support one these.
 */
static VkFormat preferred_color_formats[] = {
    VK_FORMAT_B8G8R8A8_SRGB,         //
    VK_FORMAT_R8G8B8A8_SRGB,         //
    VK_FORMAT_B8G8R8A8_UNORM,        //
    VK_FORMAT_R8G8B8A8_UNORM,        //
    VK_FORMAT_A8B8G8R8_UNORM_PACK32, // Just in case.
};


/*
 *
 * Pre declare functions.
 *
 */

static void
comp_target_swapchain_create_image_views(struct comp_target_swapchain *cts);

static void
comp_target_swapchain_destroy_image_views(struct comp_target_swapchain *cts);

static void
comp_target_swapchain_destroy_old(struct comp_target_swapchain *cts, VkSwapchainKHR old);

static VkExtent2D
comp_target_swapchain_select_extent(struct comp_target_swapchain *cts,
                                    VkSurfaceCapabilitiesKHR caps,
                                    uint32_t preferred_width,
                                    uint32_t preferred_height);

static bool
_find_surface_format(struct comp_target_swapchain *cts, VkSurfaceKHR surface, VkSurfaceFormatKHR *format);

static bool
_check_surface_present_mode(struct comp_target_swapchain *cts, VkSurfaceKHR surface, VkPresentModeKHR present_mode);


/*
 *
 * Vulkan functions.
 *
 */

static inline struct vk_bundle *
get_vk(struct comp_target_swapchain *cts)
{
	return &cts->base.c->base.vk;
}

#if defined(VK_EXT_display_surface_counter) && defined(VK_EXT_display_control)
static bool
check_surface_counter_caps(struct comp_target *ct, struct vk_bundle *vk, struct comp_target_swapchain *cts)
{
	if (!vk->has_EXT_display_surface_counter) {
		return true;
	}

	VkSurfaceCapabilities2EXT caps = {
	    .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES2_EXT,
	};
	VkResult ret = vk->vkGetPhysicalDeviceSurfaceCapabilities2EXT(vk->physical_device, cts->surface.handle, &caps);

	if (ret != VK_SUCCESS) {
		COMP_ERROR(ct->c, "vkGetPhysicalDeviceSurfaceCapabilities2EXT: %s", vk_result_string(ret));
		return false;
	}

	cts->surface.surface_counter_flags = caps.supportedSurfaceCounters;
	COMP_DEBUG(ct->c, "Supported surface counter flags: %d", caps.supportedSurfaceCounters);

	return true;
}

#endif

static void
comp_target_swapchain_create_images(struct comp_target *ct,
                                    uint32_t preferred_width,
                                    uint32_t preferred_height,
                                    VkFormat color_format,
                                    VkColorSpaceKHR color_space,
                                    VkImageUsageFlags image_usage,
                                    VkPresentModeKHR present_mode)
{
	struct comp_target_swapchain *cts = (struct comp_target_swapchain *)ct;
	struct vk_bundle *vk = get_vk(cts);
	VkBool32 supported;
	VkResult ret;

	uint64_t now_ns = os_monotonic_get_ns();
	// Some platforms really don't like the pacing_compositor code.
	bool use_display_timing_if_available = cts->timing_usage == COMP_TARGET_USE_DISPLAY_IF_AVAILABLE;
	if (cts->upc == NULL && use_display_timing_if_available && vk->has_GOOGLE_display_timing) {
		u_pc_display_timing_create(ct->c->settings.nominal_frame_interval_ns,
		                           &U_PC_DISPLAY_TIMING_CONFIG_DEFAULT, &cts->upc);
	} else if (cts->upc == NULL) {
		u_pc_fake_create(ct->c->settings.nominal_frame_interval_ns, now_ns, &cts->upc);
	}

	// Free old image views.
	comp_target_swapchain_destroy_image_views(cts);

	VkSwapchainKHR old_swapchain_handle = cts->swapchain.handle;

	cts->base.image_count = 0;
	cts->swapchain.handle = VK_NULL_HANDLE;
	cts->present_mode = present_mode;
	cts->preferred.color_format = color_format;
	cts->preferred.color_space = color_space;


	// Sanity check.
	ret = vk->vkGetPhysicalDeviceSurfaceSupportKHR( //
	    vk->physical_device,                        // physicalDevice
	    vk->queue_family_index,                     // queueFamilyIndex
	    cts->surface.handle,                        // surface
	    &supported);                                // pSupported
	if (ret != VK_SUCCESS) {
		COMP_ERROR(ct->c, "vkGetPhysicalDeviceSurfaceSupportKHR: %s", vk_result_string(ret));
	} else if (!supported) {
		COMP_ERROR(ct->c, "vkGetPhysicalDeviceSurfaceSupportKHR: Surface not supported!");
	}

	// More sanity checks.
	if (!_check_surface_present_mode(cts, cts->surface.handle, cts->present_mode)) {
		// Free old.
		comp_target_swapchain_destroy_old(cts, old_swapchain_handle);
		return;
	}

	// Find the correct format.
	if (!_find_surface_format(cts, cts->surface.handle, &cts->surface.format)) {
		// Free old.
		comp_target_swapchain_destroy_old(cts, old_swapchain_handle);
		return;
	}

	// Get the caps first.
	VkSurfaceCapabilitiesKHR surface_caps;
	ret = vk->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->physical_device, cts->surface.handle, &surface_caps);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(ct->c, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR: %s", vk_result_string(ret));

		// Free old.
		comp_target_swapchain_destroy_old(cts, old_swapchain_handle);
		return;
	}

	// Get the extents of the swapchain.
	VkExtent2D extent = comp_target_swapchain_select_extent(cts, surface_caps, preferred_width, preferred_height);

	if (surface_caps.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
	    surface_caps.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
		COMP_DEBUG(ct->c, "Swapping width and height, since we are going to pre rotate");
		uint32_t w2 = extent.width;
		uint32_t h2 = extent.height;
		extent.width = h2;
		extent.height = w2;
	}

	// Create the swapchain now.
	VkSwapchainCreateInfoKHR swapchain_info = {
	    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
	    .surface = cts->surface.handle,
	    .minImageCount = surface_caps.minImageCount,
	    .imageFormat = cts->surface.format.format,
	    .imageColorSpace = cts->surface.format.colorSpace,
	    .imageExtent =
	        {
	            .width = extent.width,
	            .height = extent.height,
	        },
	    .imageArrayLayers = 1,
	    .imageUsage = image_usage,
	    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
	    .queueFamilyIndexCount = 0,
	    .preTransform = surface_caps.currentTransform,
	    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
	    .presentMode = cts->present_mode,
	    .clipped = VK_TRUE,
	    .oldSwapchain = old_swapchain_handle,
	};

	ret = vk->vkCreateSwapchainKHR(vk->device, &swapchain_info, NULL, &cts->swapchain.handle);

	// Always destroy the old.
	comp_target_swapchain_destroy_old(cts, old_swapchain_handle);

	if (ret != VK_SUCCESS) {
		COMP_ERROR(ct->c, "vkCreateSwapchainKHR: %s", vk_result_string(ret));
		return;
	}


	/*
	 * Set target info.
	 */

	cts->base.width = extent.width;
	cts->base.height = extent.height;
	cts->base.format = cts->surface.format.format;
	cts->base.surface_transform = surface_caps.currentTransform;

	comp_target_swapchain_create_image_views(cts);
#ifdef VK_EXT_display_control
	if (!check_surface_counter_caps(ct, vk, cts)) {
		COMP_ERROR(ct->c, "Failed to query surface counter capabilities");
	}
#endif
}

static VkExtent2D
comp_target_swapchain_select_extent(struct comp_target_swapchain *cts,
                                    VkSurfaceCapabilitiesKHR caps,
                                    uint32_t preferred_width,
                                    uint32_t preferred_height)
{
	// If width (and height) equals the special value 0xFFFFFFFF,
	// the size of the surface will be set by the swapchain
	if (caps.currentExtent.width == (uint32_t)-1) {
		assert(preferred_width > 0 && preferred_height > 0);

		VkExtent2D extent = {
		    .width = preferred_width,
		    .height = preferred_height,
		};
		return extent;
	}

	if (caps.currentExtent.width != preferred_width || //
	    caps.currentExtent.height != preferred_height) {
		COMP_DEBUG(cts->base.c, "Using swap chain extent dimensions %dx%d instead of requested %dx%d.",
		           caps.currentExtent.width,  //
		           caps.currentExtent.height, //
		           preferred_width,           //
		           preferred_height);         //
	}

	return caps.currentExtent;
}

static void
comp_target_swapchain_destroy_old(struct comp_target_swapchain *cts, VkSwapchainKHR old)
{
	struct vk_bundle *vk = get_vk(cts);

	if (old != VK_NULL_HANDLE) {
		vk->vkDestroySwapchainKHR(vk->device, old, NULL);
	}
}

static bool
comp_target_swapchain_has_images(struct comp_target *ct)
{
	struct comp_target_swapchain *cts = (struct comp_target_swapchain *)ct;
	return cts->surface.handle != VK_NULL_HANDLE && cts->swapchain.handle != VK_NULL_HANDLE;
}

static VkResult
comp_target_swapchain_acquire_next_image(struct comp_target *ct, VkSemaphore semaphore, uint32_t *out_index)
{
	struct comp_target_swapchain *cts = (struct comp_target_swapchain *)ct;
	struct vk_bundle *vk = get_vk(cts);

	if (!comp_target_swapchain_has_images(ct)) {
		//! @todo what error to return here?
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	return vk->vkAcquireNextImageKHR( //
	    vk->device,                   // device
	    cts->swapchain.handle,        // swapchain
	    UINT64_MAX,                   // timeout
	    semaphore,                    // semaphore
	    VK_NULL_HANDLE,               // fence
	    out_index);                   // pImageIndex
}

static VkResult
comp_target_swapchain_present(struct comp_target *ct,
                              VkQueue queue,
                              uint32_t index,
                              VkSemaphore semaphore,
                              uint64_t desired_present_time_ns,
                              uint64_t present_slop_ns)
{
	struct comp_target_swapchain *cts = (struct comp_target_swapchain *)ct;
	struct vk_bundle *vk = get_vk(cts);

	assert(cts->current_frame_id >= 0);
	assert(cts->current_frame_id <= UINT32_MAX);

	VkPresentTimeGOOGLE times = {
	    .presentID = (uint32_t)cts->current_frame_id,
	    .desiredPresentTime = desired_present_time_ns - present_slop_ns,
	};

	VkPresentTimesInfoGOOGLE timings = {
	    .sType = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE,
	    .swapchainCount = 1,
	    .pTimes = &times,
	};

	VkPresentInfoKHR presentInfo = {
	    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
	    .pNext = vk->has_GOOGLE_display_timing ? &timings : NULL,
	    .waitSemaphoreCount = 1,
	    .pWaitSemaphores = &semaphore,
	    .swapchainCount = 1,
	    .pSwapchains = &cts->swapchain.handle,
	    .pImageIndices = &index,
	};

	return vk->vkQueuePresentKHR(queue, &presentInfo);
}

static bool
comp_target_swapchain_check_ready(struct comp_target *ct)
{
	struct comp_target_swapchain *cts = (struct comp_target_swapchain *)ct;
	return cts->surface.handle != VK_NULL_HANDLE;
}

static bool
_find_surface_format(struct comp_target_swapchain *cts, VkSurfaceKHR surface, VkSurfaceFormatKHR *format)
{
	struct vk_bundle *vk = get_vk(cts);
	uint32_t format_count;
	VkSurfaceFormatKHR *formats = NULL;
	VkResult ret;

	ret = vk->vkGetPhysicalDeviceSurfaceFormatsKHR(vk->physical_device, surface, &format_count, NULL);

	if (format_count != 0) {
		formats = U_TYPED_ARRAY_CALLOC(VkSurfaceFormatKHR, format_count);
		vk->vkGetPhysicalDeviceSurfaceFormatsKHR(vk->physical_device, surface, &format_count, formats);
	} else {
		COMP_ERROR(cts->base.c, "Could not enumerate surface formats. '%s'", vk_result_string(ret));
		return false;
	}

	// Dump formats
	for (uint32_t i = 0; i < format_count; i++) {
		COMP_SPEW(cts->base.c, "VkSurfaceFormatKHR: %i [%s, %s]", i, vk_format_string(formats[i].format),
		          vk_color_space_string(formats[i].colorSpace));
	}

	VkSurfaceFormatKHR *formats_for_colorspace = NULL;
	formats_for_colorspace = U_TYPED_ARRAY_CALLOC(VkSurfaceFormatKHR, format_count);

	uint32_t format_for_colorspace_count = 0;
	uint32_t pref_format_count = ARRAY_SIZE(preferred_color_formats);

	// Gather formats that match our color space, we will select
	// from these in preference to others.


	for (uint32_t i = 0; i < format_count; i++) {
		if (formats[i].colorSpace == cts->preferred.color_space) {
			formats_for_colorspace[format_for_colorspace_count] = formats[i];
			format_for_colorspace_count++;
		}
	}

	if (format_for_colorspace_count > 0) {
		// we have at least one format with our preferred colorspace
		// if we have one that is on our preferred formats list, use it

		for (uint32_t i = 0; i < format_for_colorspace_count; i++) {
			if (formats_for_colorspace[i].format == cts->preferred.color_format) {
				// perfect match.
				*format = formats_for_colorspace[i];
				goto cleanup;
			}
		}

		// we don't have our swapchain default format and colorspace,
		// but we may have at least one preferred format with the
		// correct colorspace.
		for (uint32_t i = 0; i < format_for_colorspace_count; i++) {
			for (uint32_t j = 0; j < pref_format_count; j++) {
				if (formats_for_colorspace[i].format == preferred_color_formats[j]) {
					*format = formats_for_colorspace[i];
					goto cleanup;
				}
			}
		}

		// are we still here? this means we have a format with our
		// preferred colorspace but we have no preferred color format -
		// maybe we only have 10/12 bpc or 15/16bpp format. return the
		// first one we have, at least its in the right color space.
		*format = formats_for_colorspace[0];
		COMP_ERROR(cts->base.c, "Returning unknown color format");
		goto cleanup;

	} else {

		// we have nothing with the preferred colorspace? we can try to
		// return a preferred format at least
		for (uint32_t i = 0; i < format_count; i++) {
			for (uint32_t j = 0; j < pref_format_count; j++) {
				if (formats[i].format == preferred_color_formats[j]) {
					*format = formats_for_colorspace[i];
					COMP_ERROR(cts->base.c,
					           "Returning known-wrong color space! Color shift may occur.");
					goto cleanup;
				}
			}
		}
		// if we are still here, we should just return the first format
		// we have. we know its the wrong colorspace, and its not on our
		// list of preferred formats, but its something.
		*format = formats[0];
		COMP_ERROR(cts->base.c,
		           "Returning fallback format! cue up some Kenny Loggins, cos we're in the DANGER ZONE!");
		goto cleanup;
	}

	COMP_ERROR(cts->base.c, "We should not be here");
	goto error;

cleanup:
	free(formats_for_colorspace);
	free(formats);

	COMP_DEBUG(cts->base.c,
	           "VkSurfaceFormatKHR"
	           "\n\tpicked: [format = %s, colorSpace = %s]"
	           "\n\tpreferred: [format = %s, colorSpace = %s]",
	           vk_format_string(format->format),                   //
	           vk_color_space_string(format->colorSpace),          //
	           vk_format_string(cts->preferred.color_format),      //
	           vk_color_space_string(cts->preferred.color_space)); //

	return true;

error:
	free(formats_for_colorspace);
	free(formats);
	return false;
}

static bool
_check_surface_present_mode(struct comp_target_swapchain *cts, VkSurfaceKHR surface, VkPresentModeKHR present_mode)
{
	struct vk_bundle *vk = get_vk(cts);
	VkResult ret;

	uint32_t present_mode_count;
	VkPresentModeKHR *present_modes;
	ret = vk->vkGetPhysicalDeviceSurfacePresentModesKHR(vk->physical_device, surface, &present_mode_count, NULL);

	if (present_mode_count != 0) {
		present_modes = U_TYPED_ARRAY_CALLOC(VkPresentModeKHR, present_mode_count);
		vk->vkGetPhysicalDeviceSurfacePresentModesKHR(vk->physical_device, surface, &present_mode_count,
		                                              present_modes);
	} else {
		COMP_ERROR(cts->base.c, "Could not enumerate present modes. '%s'", vk_result_string(ret));
		return false;
	}

	for (uint32_t i = 0; i < present_mode_count; i++) {
		if (present_modes[i] == present_mode) {
			free(present_modes);
			return true;
		}
	}

	free(present_modes);
	COMP_ERROR(cts->base.c, "Requested present mode not supported.\n");
	return false;
}

static void
comp_target_swapchain_destroy_image_views(struct comp_target_swapchain *cts)
{
	if (cts->base.images == NULL) {
		return;
	}

	struct vk_bundle *vk = get_vk(cts);

	for (uint32_t i = 0; i < cts->base.image_count; i++) {
		if (cts->base.images[i].view == VK_NULL_HANDLE) {
			continue;
		}

		vk->vkDestroyImageView(vk->device, cts->base.images[i].view, NULL);
		cts->base.images[i].view = VK_NULL_HANDLE;
	}

	free(cts->base.images);
	cts->base.images = NULL;
}

static void
comp_target_swapchain_create_image_views(struct comp_target_swapchain *cts)
{
	struct vk_bundle *vk = get_vk(cts);

	vk->vkGetSwapchainImagesKHR( //
	    vk->device,              // device
	    cts->swapchain.handle,   // swapchain
	    &cts->base.image_count,  // pSwapchainImageCount
	    NULL);                   // pSwapchainImages
	assert(cts->base.image_count > 0);
	COMP_DEBUG(cts->base.c, "Creating %d image views.", cts->base.image_count);

	VkImage *images = U_TYPED_ARRAY_CALLOC(VkImage, cts->base.image_count);
	vk->vkGetSwapchainImagesKHR( //
	    vk->device,              // device
	    cts->swapchain.handle,   // swapchain
	    &cts->base.image_count,  // pSwapchainImageCount
	    images);                 // pSwapchainImages

	comp_target_swapchain_destroy_image_views(cts);

	cts->base.images = U_TYPED_ARRAY_CALLOC(struct comp_target_image, cts->base.image_count);

	VkImageSubresourceRange subresource_range = {
	    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	    .baseMipLevel = 0,
	    .levelCount = 1,
	    .baseArrayLayer = 0,
	    .layerCount = 1,
	};

	for (uint32_t i = 0; i < cts->base.image_count; i++) {
		cts->base.images[i].handle = images[i];
		vk_create_view(                 //
		    vk,                         // vk_bundle
		    cts->base.images[i].handle, // image
		    cts->surface.format.format, // format
		    subresource_range,          // subresource_range
		    &cts->base.images[i].view); // out_view
	}

	free(images);
}


/*
 *
 * Timing functions.
 *
 */

static void
comp_target_swapchain_calc_frame_pacing(struct comp_target *ct,
                                        int64_t *out_frame_id,
                                        uint64_t *out_wake_up_time_ns,
                                        uint64_t *out_desired_present_time_ns,
                                        uint64_t *out_present_slop_ns,
                                        uint64_t *out_predicted_display_time_ns)
{
	struct comp_target_swapchain *cts = (struct comp_target_swapchain *)ct;

	int64_t frame_id = -1;
	uint64_t wake_up_time_ns = 0;
	uint64_t desired_present_time_ns = 0;
	uint64_t present_slop_ns = 0;
	uint64_t predicted_display_time_ns = 0;
	uint64_t predicted_display_period_ns = 0;
	uint64_t min_display_period_ns = 0;
	uint64_t now_ns = os_monotonic_get_ns();

	u_pc_predict(cts->upc,                     //
	             now_ns,                       //
	             &frame_id,                    //
	             &wake_up_time_ns,             //
	             &desired_present_time_ns,     //
	             &present_slop_ns,             //
	             &predicted_display_time_ns,   //
	             &predicted_display_period_ns, //
	             &min_display_period_ns);      //

	cts->current_frame_id = frame_id;

	*out_frame_id = frame_id;
	*out_wake_up_time_ns = wake_up_time_ns;
	*out_desired_present_time_ns = desired_present_time_ns;
	*out_predicted_display_time_ns = predicted_display_time_ns;
	*out_present_slop_ns = present_slop_ns;
}

static void
comp_target_swapchain_mark_timing_point(struct comp_target *ct,
                                        enum comp_target_timing_point point,
                                        int64_t frame_id,
                                        uint64_t when_ns)
{
	struct comp_target_swapchain *cts = (struct comp_target_swapchain *)ct;
	assert(frame_id == cts->current_frame_id);

	switch (point) {
	case COMP_TARGET_TIMING_POINT_WAKE_UP:
		u_pc_mark_point(cts->upc, U_TIMING_POINT_WAKE_UP, cts->current_frame_id, when_ns);
		break;
	case COMP_TARGET_TIMING_POINT_BEGIN:
		u_pc_mark_point(cts->upc, U_TIMING_POINT_BEGIN, cts->current_frame_id, when_ns);
		break;
	case COMP_TARGET_TIMING_POINT_SUBMIT:
		u_pc_mark_point(cts->upc, U_TIMING_POINT_SUBMIT, cts->current_frame_id, when_ns);
		break;
	default: assert(false);
	}
}

static VkResult
comp_target_swapchain_update_timings(struct comp_target *ct)
{
	struct comp_target_swapchain *cts = (struct comp_target_swapchain *)ct;
	struct vk_bundle *vk = get_vk(cts);

	if (!vk->has_GOOGLE_display_timing) {
		return VK_SUCCESS;
	}

	if (cts->swapchain.handle == VK_NULL_HANDLE) {
		return VK_SUCCESS;
	}

	uint32_t count = 0;
	vk->vkGetPastPresentationTimingGOOGLE( //
	    vk->device,                        //
	    cts->swapchain.handle,             //
	    &count,                            //
	    NULL);                             //
	if (count <= 0) {
		return VK_SUCCESS;
	}

	VkPastPresentationTimingGOOGLE *timings = U_TYPED_ARRAY_CALLOC(VkPastPresentationTimingGOOGLE, count);
	vk->vkGetPastPresentationTimingGOOGLE( //
	    vk->device,                        //
	    cts->swapchain.handle,             //
	    &count,                            //
	    timings);                          //
	uint64_t now_ns = os_monotonic_get_ns();
	for (uint32_t i = 0; i < count; i++) {
		u_pc_info(cts->upc,                       //
		          timings[i].presentID,           //
		          timings[i].desiredPresentTime,  //
		          timings[i].actualPresentTime,   //
		          timings[i].earliestPresentTime, //
		          timings[i].presentMargin,       //
		          now_ns);                        //
	}
	free(timings);
	return VK_SUCCESS;
}


/*
 *
 * 'Exported' functions.
 *
 */

void
comp_target_swapchain_cleanup(struct comp_target_swapchain *cts)
{
	struct vk_bundle *vk = get_vk(cts);

	comp_target_swapchain_destroy_image_views(cts);

	if (cts->swapchain.handle != VK_NULL_HANDLE) {
		vk->vkDestroySwapchainKHR( //
		    vk->device,            // device
		    cts->swapchain.handle, // swapchain
		    NULL);                 //
		cts->swapchain.handle = VK_NULL_HANDLE;
	}

	if (cts->surface.handle != VK_NULL_HANDLE) {
		vk->vkDestroySurfaceKHR( //
		    vk->instance,        // instance
		    cts->surface.handle, // surface
		    NULL);               //
		cts->swapchain.handle = VK_NULL_HANDLE;
	}

	u_pc_destroy(&cts->upc);
}

void
comp_target_swapchain_init_and_set_fnptrs(struct comp_target_swapchain *cts,
                                          enum comp_target_display_timing_usage timing_usage)
{
	cts->timing_usage = timing_usage;
	cts->base.check_ready = comp_target_swapchain_check_ready;
	cts->base.create_images = comp_target_swapchain_create_images;
	cts->base.has_images = comp_target_swapchain_has_images;
	cts->base.acquire = comp_target_swapchain_acquire_next_image;
	cts->base.present = comp_target_swapchain_present;
	cts->base.calc_frame_pacing = comp_target_swapchain_calc_frame_pacing;
	cts->base.mark_timing_point = comp_target_swapchain_mark_timing_point;
	cts->base.update_timings = comp_target_swapchain_update_timings;
}
