// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Direct mode window code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include <inttypes.h>

#include "util/u_misc.h"

#include "xrt/xrt_compiler.h"
#include "main/comp_window.h"


/*
 *
 * Private structs
 *
 */

/*!
 * Probed display.
 */
struct comp_window_direct_nvidia_display
{
	char *name;
	VkDisplayPropertiesKHR display_properties;
	VkDisplayKHR display;
};

/*!
 * Direct mode "window" into a device, using Vulkan direct mode extension
 * and xcb.
 */
struct comp_window_direct_nvidia
{
	struct comp_window base;

	Display *dpy;
	struct comp_window_direct_nvidia_display *nv_displays;
	uint16_t num_displays;
};



/*
 *
 * Pre decalre functions
 *
 */

static void
comp_window_direct_nvidia_destroy(struct comp_window *w);

static bool
comp_window_direct_nvidia_init(struct comp_window *w);

static struct comp_window_direct_nvidia_display *
comp_window_direct_nvidia_current_display(struct comp_window_direct_nvidia *w);

static VkDisplayModeKHR
comp_window_direct_nvidia_get_primary_display_mode(
    struct comp_window_direct_nvidia *w, VkDisplayKHR display);

static VkDisplayPlaneAlphaFlagBitsKHR
choose_alpha_mode(VkDisplayPlaneAlphaFlagsKHR flags);

static VkResult
comp_window_direct_nvidia_create_surface(struct comp_window_direct_nvidia *w,
                                         VkInstance instance,
                                         VkSurfaceKHR *surface,
                                         uint32_t width,
                                         uint32_t height);

static bool
comp_window_direct_nvidia_init_swapchain(struct comp_window *w,
                                         uint32_t width,
                                         uint32_t height);

static int
comp_window_direct_nvidia_connect(struct comp_window_direct_nvidia *w);

static VkResult
comp_window_direct_nvidia_acquire_xlib_display(
    struct comp_window_direct_nvidia *w, VkDisplayKHR display);

/*
 *
 * Functions.
 *
 */

static void
_flush(struct comp_window *w)
{}

static void
_update_window_title(struct comp_window *w, const char *title)
{}

struct comp_window *
comp_window_direct_nvidia_create(struct comp_compositor *c)
{
	struct comp_window_direct_nvidia *w =
	    U_TYPED_CALLOC(struct comp_window_direct_nvidia);

	w->base.name = "direct";
	w->base.destroy = comp_window_direct_nvidia_destroy;
	w->base.flush = _flush;
	w->base.init = comp_window_direct_nvidia_init;
	w->base.init_swapchain = comp_window_direct_nvidia_init_swapchain;
	w->base.update_window_title = _update_window_title;
	w->base.c = c;

	return &w->base;
}

static void
comp_window_direct_nvidia_destroy(struct comp_window *w)
{
	struct comp_window_direct_nvidia *w_direct =
	    (struct comp_window_direct_nvidia *)w;

	for (uint32_t i = 0; i < w_direct->num_displays; i++) {
		struct comp_window_direct_nvidia_display *d =
		    &w_direct->nv_displays[i];
		d->display = VK_NULL_HANDLE;
		free(d->name);
	}

	if (w_direct->nv_displays != NULL)
		free(w_direct->nv_displays);

	if (w_direct->dpy) {
		XCloseDisplay(w_direct->dpy);
		w_direct->dpy = NULL;
	}

	free(w);
}

static bool
append_nvidia_entry_on_match(struct comp_window_direct_nvidia *w,
                             const char *wl_entry,
                             struct VkDisplayPropertiesKHR *disp)
{
	unsigned long wl_entry_length = strlen(wl_entry);
	unsigned long disp_entry_length = strlen(disp->displayName);
	if (disp_entry_length < wl_entry_length)
		return false;

	if (strncmp(wl_entry, disp->displayName, wl_entry_length) != 0)
		return false;

	// we have a match with this whitelist entry.
	w->base.c->settings.width = disp->physicalResolution.width;
	w->base.c->settings.height = disp->physicalResolution.height;
	struct comp_window_direct_nvidia_display d = {
	    .name = U_TYPED_ARRAY_CALLOC(char, disp_entry_length + 1),
	    .display_properties = *disp,
	    .display = disp->display};

	memcpy(d.name, disp->displayName, disp_entry_length);
	d.name[disp_entry_length] = '\0';

	w->num_displays += 1;

	U_ARRAY_REALLOC_OR_FREE(w->nv_displays,
	                        struct comp_window_direct_nvidia_display,
	                        w->num_displays);

	if (w->nv_displays == NULL)
		COMP_ERROR(w->base.c, "Unable to reallocate randr_displays");

	w->nv_displays[w->num_displays - 1] = d;

	return true;
}

static bool
comp_window_direct_nvidia_init(struct comp_window *w)
{
	// Sanity check.
	if (w->c->vk.instance == VK_NULL_HANDLE) {
		COMP_ERROR(w->c, "Vulkan not initialized before NVIDIA init!");
		return false;
	}

	struct comp_window_direct_nvidia *w_direct =
	    (struct comp_window_direct_nvidia *)w;

	if (!comp_window_direct_nvidia_connect(w_direct)) {
		return false;
	}

	struct vk_bundle comp_vk = w->c->vk;

	// find our display using nvidia whitelist, enumerate its modes, and
	// pick the best one get a list of attached displays
	uint32_t display_count;
	if (comp_vk.vkGetPhysicalDeviceDisplayPropertiesKHR(
	        comp_vk.physical_device, &display_count, NULL) != VK_SUCCESS) {
		COMP_ERROR(w->c, "Failed to get vulkan display count");
		return false;
	}

	if (display_count == 0) {
		COMP_ERROR(w->c, "NVIDIA: No Vulkan displays found.");
		return false;
	}

	struct VkDisplayPropertiesKHR *display_props =
	    U_TYPED_ARRAY_CALLOC(VkDisplayPropertiesKHR, display_count);

	if (display_props && comp_vk.vkGetPhysicalDeviceDisplayPropertiesKHR(
	                         comp_vk.physical_device, &display_count,
	                         display_props) != VK_SUCCESS) {
		COMP_ERROR(w->c, "Failed to get display properties");
		free(display_props);
		return false;
	}

	// TODO: what if we have multiple whitelisted HMD displays connected?
	for (uint32_t i = 0; i < display_count; i++) {
		struct VkDisplayPropertiesKHR disp = *(display_props + i);
		// check this display against our whitelist
		for (uint32_t j = 0; j < ARRAY_SIZE(NV_DIRECT_WHITELIST); j++)
			if (append_nvidia_entry_on_match(
			        w_direct, NV_DIRECT_WHITELIST[j], &disp))
				break;
	}

	if (w_direct->num_displays == 0) {
		COMP_ERROR(w->c,
		           "NVIDIA: No machting displays found. "
		           "Is your headset whitelisted?");

		COMP_ERROR(w->c, "== Whitelist ==");
		for (uint32_t i = 0; i < ARRAY_SIZE(NV_DIRECT_WHITELIST); i++)
			COMP_ERROR(w->c, "%s", NV_DIRECT_WHITELIST[i]);

		COMP_ERROR(w->c, "== Available ==");
		for (uint32_t i = 0; i < display_count; i++)
			COMP_ERROR(w->c, "%s", display_props[i].displayName);

		free(display_props);
		return false;
	}

	free(display_props);

	return true;
}

static struct comp_window_direct_nvidia_display *
comp_window_direct_nvidia_current_display(struct comp_window_direct_nvidia *w)
{
	int index = w->base.c->settings.display;
	if (index == -1)
		index = 0;

	if (w->num_displays <= (uint32_t)index)
		return NULL;

	return &w->nv_displays[index];
}

static int
choose_best_vk_mode_auto(struct comp_window_direct_nvidia *w,
                         VkDisplayModePropertiesKHR *mode_properties,
                         int mode_count)
{
	if (mode_count == 1)
		return 0;

	int best_index = 0;

	// First priority: choose mode that maximizes rendered pixels.
	// Second priority: choose mode with highest refresh rate.
	for (int i = 1; i < mode_count; i++) {
		VkDisplayModeParametersKHR current =
		    mode_properties[i].parameters;
		COMP_DEBUG(w->base.c, "Available Vk direct mode %d: %dx%d@%.2f",
		           i, current.visibleRegion.width,
		           current.visibleRegion.height,
		           (float)current.refreshRate / 1000.);


		VkDisplayModeParametersKHR best =
		    mode_properties[best_index].parameters;

		int best_pixels =
		    best.visibleRegion.width * best.visibleRegion.height;
		int pixels =
		    current.visibleRegion.width * current.visibleRegion.height;
		if (pixels > best_pixels) {
			best_index = i;
		} else if (pixels == best_pixels &&
		           current.refreshRate > best.refreshRate) {
			best_index = i;
		}
	}
	VkDisplayModeParametersKHR best =
	    mode_properties[best_index].parameters;
	COMP_DEBUG(w->base.c, "Auto choosing Vk direct mode %d: %dx%d@%.2f",
	           best_index, best.visibleRegion.width,
	           best.visibleRegion.width, (float)best.refreshRate / 1000.);
	return best_index;
}

static void
print_modes(struct comp_window_direct_nvidia *w,
            VkDisplayModePropertiesKHR *mode_properties,
            int mode_count)
{
	COMP_PRINT_MODE(w->base.c, "Available Vk modes for direct mode");
	for (int i = 0; i < mode_count; i++) {
		VkDisplayModePropertiesKHR props = mode_properties[i];
		uint16_t width = props.parameters.visibleRegion.width;
		uint16_t height = props.parameters.visibleRegion.height;
		float refresh = (float)props.parameters.refreshRate / 1000.;

		COMP_PRINT_MODE(w->base.c, "| %2d | %dx%d@%.2f", i, width,
		                height, refresh);
	}
	COMP_PRINT_MODE(w->base.c, "Listed %d modes", mode_count);
}

static VkDisplayModeKHR
comp_window_direct_nvidia_get_primary_display_mode(
    struct comp_window_direct_nvidia *w, VkDisplayKHR display)
{
	struct vk_bundle *vk = w->base.swapchain.vk;
	uint32_t mode_count;
	VkResult ret;

	ret = vk->vkGetDisplayModePropertiesKHR(
	    w->base.swapchain.vk->physical_device, display, &mode_count, NULL);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c, "vkGetDisplayModePropertiesKHR: %s",
		           vk_result_string(ret));
		return VK_NULL_HANDLE;
	}

	COMP_DEBUG(w->base.c, "Found %d modes", mode_count);

	VkDisplayModePropertiesKHR *mode_properties =
	    U_TYPED_ARRAY_CALLOC(VkDisplayModePropertiesKHR, mode_count);
	ret = vk->vkGetDisplayModePropertiesKHR(
	    w->base.swapchain.vk->physical_device, display, &mode_count,
	    mode_properties);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c, "vkGetDisplayModePropertiesKHR: %s",
		           vk_result_string(ret));
		free(mode_properties);
		return VK_NULL_HANDLE;
	}

	print_modes(w, mode_properties, mode_count);


	int chosen_mode = 0;

	int desired_mode = w->base.c->settings.desired_mode;
	if (desired_mode + 1 > (int)mode_count) {
		COMP_ERROR(w->base.c,
		           "Requested mode index %d, but max is %d. Falling "
		           "back to automatic mode selection",
		           desired_mode, mode_count);
		chosen_mode =
		    choose_best_vk_mode_auto(w, mode_properties, mode_count);
	} else if (desired_mode < 0) {
		chosen_mode =
		    choose_best_vk_mode_auto(w, mode_properties, mode_count);
	} else {
		COMP_DEBUG(w->base.c, "Using manually chosen mode %d",
		           desired_mode);
		chosen_mode = desired_mode;
	}

	VkDisplayModePropertiesKHR props = mode_properties[chosen_mode];

	COMP_DEBUG(w->base.c, "found display mode %dx%d@%.2f",
	           props.parameters.visibleRegion.width,
	           props.parameters.visibleRegion.height,
	           (float)props.parameters.refreshRate / 1000.);

	int64_t new_frame_interval =
	    1000. * 1000. * 1000. * 1000. / props.parameters.refreshRate;

	COMP_DEBUG(
	    w->base.c,
	    "Updating compositor settings nominal frame interval from %" PRIu64
	    " (%f Hz) to %" PRIu64 " (%f Hz)",
	    w->base.c->settings.nominal_frame_interval_ns,
	    1000. * 1000. * 1000. /
	        (float)w->base.c->settings.nominal_frame_interval_ns,
	    new_frame_interval, (float)props.parameters.refreshRate / 1000.);

	w->base.c->settings.nominal_frame_interval_ns = new_frame_interval;

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

static VkResult
comp_window_direct_nvidia_create_surface(struct comp_window_direct_nvidia *w,
                                         VkInstance instance,
                                         VkSurfaceKHR *surface,
                                         uint32_t width,
                                         uint32_t height)
{
	struct comp_window_direct_nvidia_display *nvd =
	    comp_window_direct_nvidia_current_display(w);
	struct vk_bundle *vk = w->base.swapchain.vk;

	VkResult ret = VK_ERROR_INCOMPATIBLE_DISPLAY_KHR;
	VkDisplayKHR _display = VK_NULL_HANDLE;

	if (nvd) {
		COMP_DEBUG(w->base.c, "Will use display: %s", nvd->name);
		ret = comp_window_direct_nvidia_acquire_xlib_display(
		    w, nvd->display);
		_display = nvd->display;
	}

	if (ret != VK_SUCCESS) {
		return ret;
	}


	// Get plane properties
	uint32_t plane_property_count;
	ret = vk->vkGetPhysicalDeviceDisplayPlanePropertiesKHR(
	    w->base.swapchain.vk->physical_device, &plane_property_count, NULL);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c,
		           "vkGetPhysicalDeviceDisplayPlanePropertiesKHR: %s",
		           vk_result_string(ret));
		return ret;
	}

	COMP_DEBUG(w->base.c, "Found %d plane properites.",
	           plane_property_count);

	VkDisplayPlanePropertiesKHR *plane_properties = U_TYPED_ARRAY_CALLOC(
	    VkDisplayPlanePropertiesKHR, plane_property_count);

	ret = vk->vkGetPhysicalDeviceDisplayPlanePropertiesKHR(
	    w->base.swapchain.vk->physical_device, &plane_property_count,
	    plane_properties);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c,
		           "vkGetPhysicalDeviceDisplayPlanePropertiesKHR: %s",
		           vk_result_string(ret));
		free(plane_properties);
		return ret;
	}

	uint32_t plane_index = 0;

	VkDisplayModeKHR display_mode =
	    comp_window_direct_nvidia_get_primary_display_mode(w, _display);

	VkDisplayPlaneCapabilitiesKHR plane_caps;
	vk->vkGetDisplayPlaneCapabilitiesKHR(
	    w->base.swapchain.vk->physical_device, display_mode, plane_index,
	    &plane_caps);

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

	VkResult result = vk->vkCreateDisplayPlaneSurfaceKHR(
	    instance, &surface_info, NULL, surface);

	free(plane_properties);

	return result;
}

static bool
comp_window_direct_nvidia_init_swapchain(struct comp_window *w,
                                         uint32_t width,
                                         uint32_t height)
{
	struct comp_window_direct_nvidia *w_direct =
	    (struct comp_window_direct_nvidia *)w;

	VkResult ret = comp_window_direct_nvidia_create_surface(
	    w_direct, w->swapchain.vk->instance, &w->swapchain.surface, width,
	    height);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->c, "Failed to create surface!");
		return false;
	}

	vk_swapchain_create(
	    &w->swapchain, width, height, w->c->settings.color_format,
	    w->c->settings.color_space, w->c->settings.present_mode);

	return true;
}

static int
comp_window_direct_nvidia_connect(struct comp_window_direct_nvidia *w)
{
	w->dpy = XOpenDisplay(NULL);
	if (w->dpy == NULL) {
		COMP_ERROR(w->base.c, "Could not open X display.");
		return false;
	}
	return true;
}

static VkResult
comp_window_direct_nvidia_acquire_xlib_display(
    struct comp_window_direct_nvidia *w, VkDisplayKHR display)
{
	struct vk_bundle *vk = w->base.swapchain.vk;
	VkResult ret;

	ret = vk->vkAcquireXlibDisplayEXT(w->base.swapchain.vk->physical_device,
	                                  w->dpy, display);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c, "vkAcquireXlibDisplayEXT: %s (%p)",
		           vk_result_string(ret), (void *)display);
	}
	return ret;
}
