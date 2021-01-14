// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common direct mode window code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include <inttypes.h>

#include "comp_window_direct.h"

#include "util/u_misc.h"


static inline struct vk_bundle *
get_vk(struct comp_target_swapchain *cts)
{
	return &cts->base.c->vk;
}

static int
choose_best_vk_mode_auto(struct comp_target *ct, VkDisplayModePropertiesKHR *mode_properties, int mode_count)
{
	if (mode_count == 1)
		return 0;

	int best_index = 0;

	// First priority: choose mode that maximizes rendered pixels.
	// Second priority: choose mode with highest refresh rate.
	for (int i = 1; i < mode_count; i++) {
		VkDisplayModeParametersKHR current = mode_properties[i].parameters;
		COMP_DEBUG(ct->c, "Available Vk direct mode %d: %dx%d@%.2f", i, current.visibleRegion.width,
		           current.visibleRegion.height, (float)current.refreshRate / 1000.);


		VkDisplayModeParametersKHR best = mode_properties[best_index].parameters;

		int best_pixels = best.visibleRegion.width * best.visibleRegion.height;
		int pixels = current.visibleRegion.width * current.visibleRegion.height;
		if (pixels > best_pixels) {
			best_index = i;
		} else if (pixels == best_pixels && current.refreshRate > best.refreshRate) {
			best_index = i;
		}
	}
	VkDisplayModeParametersKHR best = mode_properties[best_index].parameters;
	COMP_DEBUG(ct->c, "Auto choosing Vk direct mode %d: %dx%d@%.2f", best_index, best.visibleRegion.width,
	           best.visibleRegion.width, (float)best.refreshRate / 1000.);
	return best_index;
}

static void
print_modes(struct comp_target *ct, VkDisplayModePropertiesKHR *mode_properties, int mode_count)
{
	COMP_PRINT_MODE(ct->c, "Available Vk modes for direct mode");
	for (int i = 0; i < mode_count; i++) {
		VkDisplayModePropertiesKHR props = mode_properties[i];
		uint16_t width = props.parameters.visibleRegion.width;
		uint16_t height = props.parameters.visibleRegion.height;
		float refresh = (float)props.parameters.refreshRate / 1000.;

		COMP_PRINT_MODE(ct->c, "| %2d | %dx%d@%.2f", i, width, height, refresh);
	}
	COMP_PRINT_MODE(ct->c, "Listed %d modes", mode_count);
}

VkDisplayModeKHR
comp_window_direct_get_primary_display_mode(struct comp_target_swapchain *cts, VkDisplayKHR display)
{
	struct vk_bundle *vk = get_vk(cts);
	struct comp_target *ct = &cts->base;
	uint32_t mode_count;
	VkResult ret;

	ret = vk->vkGetDisplayModePropertiesKHR(vk->physical_device, display, &mode_count, NULL);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(ct->c, "vkGetDisplayModePropertiesKHR: %s", vk_result_string(ret));
		return VK_NULL_HANDLE;
	}

	COMP_DEBUG(ct->c, "Found %d modes", mode_count);

	VkDisplayModePropertiesKHR *mode_properties = U_TYPED_ARRAY_CALLOC(VkDisplayModePropertiesKHR, mode_count);
	ret = vk->vkGetDisplayModePropertiesKHR(vk->physical_device, display, &mode_count, mode_properties);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(ct->c, "vkGetDisplayModePropertiesKHR: %s", vk_result_string(ret));
		free(mode_properties);
		return VK_NULL_HANDLE;
	}

	print_modes(ct, mode_properties, mode_count);


	int chosen_mode = 0;

	int desired_mode = ct->c->settings.desired_mode;
	if (desired_mode + 1 > (int)mode_count) {
		COMP_ERROR(ct->c,
		           "Requested mode index %d, but max is %d. Falling "
		           "back to automatic mode selection",
		           desired_mode, mode_count);
		chosen_mode = choose_best_vk_mode_auto(ct, mode_properties, mode_count);
	} else if (desired_mode < 0) {
		chosen_mode = choose_best_vk_mode_auto(ct, mode_properties, mode_count);
	} else {
		COMP_DEBUG(ct->c, "Using manually chosen mode %d", desired_mode);
		chosen_mode = desired_mode;
	}

	VkDisplayModePropertiesKHR props = mode_properties[chosen_mode];

	COMP_DEBUG(ct->c, "found display mode %dx%d@%.2f", props.parameters.visibleRegion.width,
	           props.parameters.visibleRegion.height, (float)props.parameters.refreshRate / 1000.);

	int64_t new_frame_interval = 1000. * 1000. * 1000. * 1000. / props.parameters.refreshRate;

	COMP_DEBUG(ct->c,
	           "Updating compositor settings nominal frame interval from %" PRIu64 " (%f Hz) to %" PRIu64
	           " (%f Hz)",
	           ct->c->settings.nominal_frame_interval_ns,
	           1000. * 1000. * 1000. / (float)ct->c->settings.nominal_frame_interval_ns, new_frame_interval,
	           (float)props.parameters.refreshRate / 1000.);

	ct->c->settings.nominal_frame_interval_ns = new_frame_interval;

	free(mode_properties);

	return props.displayMode;
}

static VkDisplayPlaneAlphaFlagBitsKHR
choose_alpha_mode(VkDisplayPlaneAlphaFlagsKHR flags)
{
	if (flags & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR) {
		return VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR;
	}
	if (flags & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR) {
		return VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR;
	}
	return VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
}

VkResult
comp_window_direct_create_surface(struct comp_target_swapchain *cts,
                                  VkDisplayKHR display,
                                  uint32_t width,
                                  uint32_t height)
{
	struct vk_bundle *vk = get_vk(cts);

	// Get plane properties
	uint32_t plane_property_count;
	VkResult ret =
	    vk->vkGetPhysicalDeviceDisplayPlanePropertiesKHR(vk->physical_device, &plane_property_count, NULL);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(cts->base.c, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR: %s", vk_result_string(ret));
		return ret;
	}

	COMP_DEBUG(cts->base.c, "Found %d plane properites.", plane_property_count);

	VkDisplayPlanePropertiesKHR *plane_properties =
	    U_TYPED_ARRAY_CALLOC(VkDisplayPlanePropertiesKHR, plane_property_count);

	ret = vk->vkGetPhysicalDeviceDisplayPlanePropertiesKHR(vk->physical_device, &plane_property_count,
	                                                       plane_properties);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(cts->base.c, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR: %s", vk_result_string(ret));
		free(plane_properties);
		return ret;
	}

	uint32_t plane_index = 0;

	VkDisplayModeKHR display_mode = comp_window_direct_get_primary_display_mode(cts, display);

	VkDisplayPlaneCapabilitiesKHR plane_caps;
	vk->vkGetDisplayPlaneCapabilitiesKHR(vk->physical_device, display_mode, plane_index, &plane_caps);

	VkDisplaySurfaceCreateInfoKHR surface_info = {
	    .sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
	    .pNext = NULL,
	    .flags = 0,
	    .displayMode = display_mode,
	    .planeIndex = plane_index,
	    .planeStackIndex = plane_properties[plane_index].currentStackIndex,
	    .transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
	    .globalAlpha = 1.0,
	    .alphaMode = choose_alpha_mode(plane_caps.supportedAlpha),
	    .imageExtent =
	        {
	            .width = width,
	            .height = height,
	        },
	};

	VkResult result = vk->vkCreateDisplayPlaneSurfaceKHR(vk->instance, &surface_info, NULL, &cts->surface.handle);

	free(plane_properties);

	return result;
}

int
comp_window_direct_connect(struct comp_target_swapchain *cts, Display **dpy)
{
	*dpy = XOpenDisplay(NULL);
	if (*dpy == NULL) {
		COMP_ERROR(cts->base.c, "Could not open X display.");
		return false;
	}
	return true;
}

VkResult
comp_window_direct_acquire_xlib_display(struct comp_target_swapchain *cts, Display *dpy, VkDisplayKHR display)
{
	struct vk_bundle *vk = get_vk(cts);
	VkResult ret;

	ret = vk->vkAcquireXlibDisplayEXT(vk->physical_device, dpy, display);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(cts->base.c, "vkAcquireXlibDisplayEXT: %s (0x%016" PRIx64 ")", vk_result_string(ret),
		           (uint64_t)display);
		if (cts->base.c->settings.window_type == WINDOW_DIRECT_NVIDIA &&
		    ret == VK_ERROR_INITIALIZATION_FAILED) {
			COMP_ERROR(cts->base.c,
			           "This can be caused by the AllowHMD "
			           "xorg.conf option. Please make sure that "
			           "AllowHMD is not set (like in '99-HMD.conf' "
			           "from OpenHMD) and that the desktop is not "
			           "currently extended to this display.");
		}
	}
	return ret;
}

bool
comp_window_direct_init_swapchain(
    struct comp_target_swapchain *cts, Display *dpy, VkDisplayKHR display, uint32_t width, uint32_t height)
{
	VkResult ret;
	ret = comp_window_direct_acquire_xlib_display(cts, dpy, display);

	if (ret != VK_SUCCESS) {
		return false;
	}

	ret = comp_window_direct_create_surface(cts, display, width, height);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(cts->base.c, "Failed to create surface! '%s'", vk_result_string(ret));
		return false;
	}

	return true;
}
