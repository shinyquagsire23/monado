// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan swapchain code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_client
 */


#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "util/u_misc.h"

#include "comp_vk_swapchain.h"


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
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_B8G8R8A8_UNORM,
    VK_FORMAT_A8B8G8R8_UNORM_PACK32,
};


/*
 *
 * Pre declare functions.
 *
 */

static void
vk_swapchain_create_image_view(struct vk_bundle *vk,
                               VkImage image,
                               VkFormat format,
                               VkImageView *view);

static void
vk_swapchain_create_image_views(struct vk_swapchain *sc);

static void
vk_swapchain_destroy_image_views(struct vk_swapchain *sc);

static void
vk_swapchain_destroy_old(struct vk_swapchain *sc, VkSwapchainKHR old);

static VkExtent2D
vk_swapchain_select_extent(struct vk_swapchain *sc,
                           VkSurfaceCapabilitiesKHR caps,
                           uint32_t width,
                           uint32_t height);

static bool
_find_surface_format(struct vk_swapchain *sc,
                     VkSurfaceKHR surface,
                     VkSurfaceFormatKHR *format);

static bool
_check_surface_present_mode(struct vk_bundle *vk,
                            VkSurfaceKHR surface,
                            VkPresentModeKHR present_mode);


/*
 *
 * Functions!
 *
 */

void
vk_swapchain_init(struct vk_swapchain *sc,
                  struct vk_bundle *vk,
                  vk_swapchain_cb dimension_cb,
                  void *priv)
{
	sc->vk = vk;
	sc->cb_priv = priv;
	sc->dimension_cb = dimension_cb;
}

void
vk_swapchain_create(struct vk_swapchain *sc,
                    uint32_t width,
                    uint32_t height,
                    VkFormat color_format,
                    VkColorSpaceKHR color_space,
                    VkPresentModeKHR present_mode)
{
	VkBool32 supported;
	VkResult ret;

	// Free old image views.
	vk_swapchain_destroy_image_views(sc);

	VkSwapchainKHR old_swap_chain = sc->swap_chain;

	sc->image_count = 0;
	sc->swap_chain = VK_NULL_HANDLE;
	sc->color_format = color_format;
	sc->color_space = color_space;
	sc->present_mode = present_mode;


	// Sanity check.
	sc->vk->vkGetPhysicalDeviceSurfaceSupportKHR(sc->vk->physical_device, 0,
	                                             sc->surface, &supported);
	if (!supported) {
		VK_ERROR(sc->vk,
		         "vkGetPhysicalDeviceSurfaceSupportKHR: "
		         "surface not supported!");
	}


	// More sanity checks.
	if (!_check_surface_present_mode(sc->vk, sc->surface,
	                                 sc->present_mode)) {
		// Free old.
		vk_swapchain_destroy_old(sc, old_swap_chain);
		return;
	}

	// Find the correct format.
	if (!_find_surface_format(sc, sc->surface, &sc->surface_format)) {
		// Free old.
		vk_swapchain_destroy_old(sc, old_swap_chain);
		return;
	}

	// Get the caps first.
	VkSurfaceCapabilitiesKHR surface_caps;
	ret = sc->vk->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
	    sc->vk->physical_device, sc->surface, &surface_caps);
	if (ret != VK_SUCCESS) {
		VK_ERROR(sc->vk,
		         "vkGetPhysicalDeviceSurfaceCapabilitiesKHR: %s",
		         vk_result_string(ret));

		// Free old.
		vk_swapchain_destroy_old(sc, old_swap_chain);
		return;
	}


	VkExtent2D extent =
	    vk_swapchain_select_extent(sc, surface_caps, width, height);

	VkSwapchainCreateInfoKHR swap_chain_info = {
	    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
	    .pNext = NULL,
	    .flags = 0,
	    .surface = sc->surface,
	    .minImageCount = surface_caps.minImageCount,
	    .imageFormat = sc->surface_format.format,
	    .imageColorSpace = sc->surface_format.colorSpace,
	    .imageExtent =
	        {
	            .width = extent.width,
	            .height = extent.height,
	        },
	    .imageArrayLayers = 1,
	    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
	    .queueFamilyIndexCount = 0,
	    .pQueueFamilyIndices = NULL,
	    .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
	    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
	    .presentMode = sc->present_mode,
	    .clipped = VK_TRUE,
	    .oldSwapchain = old_swap_chain,
	};

	ret = sc->vk->vkCreateSwapchainKHR(sc->vk->device, &swap_chain_info,
	                                   NULL, &sc->swap_chain);

	// Always destroy the old.
	vk_swapchain_destroy_old(sc, old_swap_chain);

	if (ret != VK_SUCCESS) {
		VK_ERROR(sc->vk, "vkCreateSwapchainKHR: %s",
		         vk_result_string(ret));
		return;
	}

	vk_swapchain_create_image_views(sc);
}

static VkExtent2D
vk_swapchain_select_extent(struct vk_swapchain *sc,
                           VkSurfaceCapabilitiesKHR caps,
                           uint32_t width,
                           uint32_t height)
{
	VkExtent2D extent;
	U_ZERO(&extent);

	// If width (and height) equals the special value 0xFFFFFFFF,
	// the size of the surface will be set by the swapchain
	if (caps.currentExtent.width == (uint32_t)-1) {
		extent.width = width;
		extent.height = height;
	} else {
		extent = caps.currentExtent;
		if (caps.currentExtent.width != width ||
		    caps.currentExtent.height != height) {
			VK_DEBUG(sc->vk,
			         "Using swap chain extent dimensions %dx%d "
			         "instead of requested %dx%d.",
			         caps.currentExtent.width,
			         caps.currentExtent.height, width, height);
			sc->dimension_cb(caps.currentExtent.width,
			                 caps.currentExtent.height,
			                 sc->cb_priv);
		}
	}
	return extent;
}

static void
vk_swapchain_destroy_old(struct vk_swapchain *sc, VkSwapchainKHR old)
{
	if (old != VK_NULL_HANDLE) {
		sc->vk->vkDestroySwapchainKHR(sc->vk->device, old, NULL);
	}
}

VkResult
vk_swapchain_acquire_next_image(struct vk_swapchain *sc,
                                VkSemaphore semaphore,
                                uint32_t *index)
{
	return sc->vk->vkAcquireNextImageKHR(sc->vk->device, sc->swap_chain,
	                                     UINT64_MAX, semaphore,
	                                     VK_NULL_HANDLE, index);
}

VkResult
vk_swapchain_present(struct vk_swapchain *sc,
                     VkQueue queue,
                     uint32_t index,
                     VkSemaphore semaphore)
{
	VkPresentInfoKHR presentInfo = {
	    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
	    .pNext = NULL,
	    .waitSemaphoreCount = 1,
	    .pWaitSemaphores = &semaphore,
	    .swapchainCount = 1,
	    .pSwapchains = &sc->swap_chain,
	    .pImageIndices = &index,
	    .pResults = NULL,
	};

	return sc->vk->vkQueuePresentKHR(queue, &presentInfo);
}

static bool
_find_surface_format(struct vk_swapchain *sc,
                     VkSurfaceKHR surface,
                     VkSurfaceFormatKHR *format)
{
	uint32_t num_formats;
	VkSurfaceFormatKHR *formats = NULL;
	sc->vk->vkGetPhysicalDeviceSurfaceFormatsKHR(
	    sc->vk->physical_device, surface, &num_formats, NULL);

	if (num_formats != 0) {
		formats = U_TYPED_ARRAY_CALLOC(VkSurfaceFormatKHR, num_formats);
		sc->vk->vkGetPhysicalDeviceSurfaceFormatsKHR(
		    sc->vk->physical_device, surface, &num_formats, formats);
	} else {
		VK_ERROR(sc->vk, "Could not enumerate surface formats.");
		return false;
	}

	VkSurfaceFormatKHR *formats_for_colorspace = NULL;
	formats_for_colorspace =
	    U_TYPED_ARRAY_CALLOC(VkSurfaceFormatKHR, num_formats);

	uint32_t num_formats_cs = 0;
	uint32_t num_pref_formats = sizeof(preferred_color_formats) /
	                            sizeof(preferred_color_formats[0]);

	// Gather formats that match our color space, we will select
	// from these in preference to others.

	for (uint32_t i = 0; i < num_formats; i++) {
		if (formats[i].colorSpace == sc->color_space) {
			formats_for_colorspace[num_formats_cs] = formats[i];
			num_formats_cs++;
		}
	}

	if (num_formats_cs > 0) {
		// we have at least one format with our preferred colorspace
		// if we have one that is on our preferred formats list, use it

		for (uint32_t i = 0; i < num_formats_cs; i++) {
			if (formats_for_colorspace[i].format ==
			    sc->color_format) {
				// perfect match.
				*format = formats_for_colorspace[i];
				goto cleanup;
			}
		}

		// we don't have our swapchain default format and colorspace,
		// but we may have at least one preferred format with the
		// correct colorspace.
		for (uint32_t i = 0; i < num_formats_cs; i++) {
			for (uint32_t j = 0; j < num_pref_formats; j++) {
				if (formats_for_colorspace[i].format ==
				    preferred_color_formats[j]) {
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
		VK_ERROR(sc->vk, "Returning unknown color format");
		goto cleanup;

	} else {

		// we have nothing with the preferred colorspace? we can try to
		// return a preferred format at least
		for (uint32_t i = 0; i < num_formats; i++) {
			for (uint32_t j = 0; j < num_pref_formats; j++) {
				if (formats[i].format ==
				    preferred_color_formats[j]) {
					*format = formats_for_colorspace[i];
					VK_ERROR(
					    sc->vk,
					    "Returning known-wrong color "
					    "space! Color shift may occur.");
					goto cleanup;
				}
			}
		}
		// if we are still here, we should just return the first format
		// we have. we know its the wrong colorspace, and its not on our
		// list of preferred formats, but its something.
		*format = formats[0];
		VK_ERROR(sc->vk,
		         "Returning fallback format! cue up some Kenny "
		         "Loggins, cos we're in the DANGER ZONE!");
		goto cleanup;
	}

	VK_ERROR(sc->vk, "We should not be here");
	goto error;

cleanup:
	free(formats_for_colorspace);
	free(formats);
	return true;

error:
	free(formats_for_colorspace);
	free(formats);
	return false;
}

static bool
_check_surface_present_mode(struct vk_bundle *vk,
                            VkSurfaceKHR surface,
                            VkPresentModeKHR present_mode)
{
	uint32_t num_present_modes;
	VkPresentModeKHR *present_modes;
	vk->vkGetPhysicalDeviceSurfacePresentModesKHR(
	    vk->physical_device, surface, &num_present_modes, NULL);

	if (num_present_modes != 0) {
		present_modes =
		    U_TYPED_ARRAY_CALLOC(VkPresentModeKHR, num_present_modes);
		vk->vkGetPhysicalDeviceSurfacePresentModesKHR(
		    vk->physical_device, surface, &num_present_modes,
		    present_modes);
	} else {
		VK_ERROR(vk, "Could not enumerate present modes.");
		return false;
	}

	for (uint32_t i = 0; i < num_present_modes; i++) {
		if (present_modes[i] == present_mode) {
			free(present_modes);
			return true;
		}
	}

	free(present_modes);
	VK_ERROR(vk, "Requested present mode not supported.\n");
	return false;
}

static void
vk_swapchain_destroy_image_views(struct vk_swapchain *sc)
{
	if (sc->buffers == NULL) {
		return;
	}

	for (uint32_t i = 0; i < sc->image_count; i++) {
		if (sc->buffers[i].view == VK_NULL_HANDLE) {
			continue;
		}

		sc->vk->vkDestroyImageView(sc->vk->device, sc->buffers[i].view,
		                           NULL);
		sc->buffers[i].view = VK_NULL_HANDLE;
	}

	free(sc->buffers);
	sc->buffers = NULL;
}

static void
vk_swapchain_create_image_views(struct vk_swapchain *sc)
{
	sc->vk->vkGetSwapchainImagesKHR(sc->vk->device, sc->swap_chain,
	                                &sc->image_count, NULL);
	assert(sc->image_count > 0);
	VK_DEBUG(sc->vk, "Creating %d image views.", sc->image_count);

	VkImage *images = U_TYPED_ARRAY_CALLOC(VkImage, sc->image_count);
	sc->vk->vkGetSwapchainImagesKHR(sc->vk->device, sc->swap_chain,
	                                &sc->image_count, images);

	vk_swapchain_destroy_image_views(sc);

	sc->buffers =
	    U_TYPED_ARRAY_CALLOC(struct vk_swapchain_buffer, sc->image_count);

	for (uint32_t i = 0; i < sc->image_count; i++) {
		sc->buffers[i].image = images[i];
		vk_swapchain_create_image_view(sc->vk, sc->buffers[i].image,
		                               sc->surface_format.format,
		                               &sc->buffers[i].view);
	}

	free(images);
}

void
vk_swapchain_cleanup(struct vk_swapchain *sc)
{
	for (uint32_t i = 0; i < sc->image_count; i++) {
		if (sc->buffers[i].view == VK_NULL_HANDLE) {
			continue;
		}

		sc->vk->vkDestroyImageView(sc->vk->device, sc->buffers[i].view,
		                           NULL);
		sc->buffers[i].view = VK_NULL_HANDLE;
	}

	if (sc->swap_chain != VK_NULL_HANDLE) {
		sc->vk->vkDestroySwapchainKHR(sc->vk->device, sc->swap_chain,
		                              NULL);
		sc->swap_chain = VK_NULL_HANDLE;
	}

	if (sc->surface != VK_NULL_HANDLE) {
		sc->vk->vkDestroySurfaceKHR(sc->vk->instance, sc->surface,
		                            NULL);
		sc->swap_chain = VK_NULL_HANDLE;
	}
}

static void
vk_swapchain_create_image_view(struct vk_bundle *vk,
                               VkImage image,
                               VkFormat format,
                               VkImageView *view)
{
	VkResult ret;

	VkImageViewCreateInfo view_create_info = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
	    .pNext = NULL,
	    .flags = 0,
	    .image = image,
	    .viewType = VK_IMAGE_VIEW_TYPE_2D,
	    .format = format,
	    .components =
	        {
	            .r = VK_COMPONENT_SWIZZLE_R,
	            .g = VK_COMPONENT_SWIZZLE_G,
	            .b = VK_COMPONENT_SWIZZLE_B,
	            .a = VK_COMPONENT_SWIZZLE_A,
	        },
	    .subresourceRange =
	        {
	            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	            .baseMipLevel = 0,
	            .levelCount = 1,
	            .baseArrayLayer = 0,
	            .layerCount = 1,
	        },
	};

	ret = vk->vkCreateImageView(vk->device, &view_create_info, NULL, view);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "vkCreateImageView: %s", vk_result_string(ret));
	}
}
