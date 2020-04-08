// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Direct mode window code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <X11/Xlib-xcb.h>
#include <X11/extensions/Xrandr.h>

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
struct comp_window_direct_randr_display
{
	char *name;
	xcb_randr_output_t output;
	xcb_randr_mode_info_t primary_mode;
	VkDisplayKHR display;
};

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
struct comp_window_direct
{
	struct comp_window base;

	Display *dpy;
	xcb_screen_t *screen;

	struct comp_window_direct_randr_display *randr_displays;
	struct comp_window_direct_nvidia_display *nv_displays;

	uint16_t num_displays;
};



/*
 *
 * Pre decalre functions
 *
 */

static void
comp_window_direct_destroy(struct comp_window *w);

XRT_MAYBE_UNUSED static void
comp_window_direct_list_randr_screens(struct comp_window_direct *w);

static bool
comp_window_direct_init_randr(struct comp_window *w);

static bool
comp_window_direct_init_nvidia(struct comp_window *w);

static struct comp_window_direct_randr_display *
comp_window_direct_current_randr_display(struct comp_window_direct *w);

static struct comp_window_direct_nvidia_display *
comp_window_direct_current_nvidia_display(struct comp_window_direct *w);

static void
comp_window_direct_flush(struct comp_window *w);

static VkDisplayModeKHR
comp_window_direct_get_primary_display_mode(struct comp_window_direct *w,
                                            VkDisplayKHR display);

static VkDisplayPlaneAlphaFlagBitsKHR
choose_alpha_mode(VkDisplayPlaneAlphaFlagsKHR flags);

static VkResult
comp_window_direct_create_surface(struct comp_window_direct *w,
                                  VkInstance instance,
                                  VkSurfaceKHR *surface,
                                  uint32_t width,
                                  uint32_t height);

static bool
comp_window_direct_init_swapchain(struct comp_window *w,
                                  uint32_t width,
                                  uint32_t height);

static int
comp_window_direct_connect(struct comp_window_direct *w);

static VkResult
comp_window_direct_acquire_xlib_display(struct comp_window_direct *w,
                                        VkDisplayKHR display);

static VkDisplayKHR
comp_window_direct_get_xlib_randr_output(struct comp_window_direct *w,
                                         RROutput output);

static void
comp_window_direct_get_randr_outputs(struct comp_window_direct *w);

static void
comp_window_direct_update_window_title(struct comp_window *w,
                                       const char *title);


/*
 *
 * Functions.
 *
 */

struct comp_window *
comp_window_direct_create(struct comp_compositor *c)
{
	struct comp_window_direct *w =
	    U_TYPED_CALLOC(struct comp_window_direct);

	w->base.name = "direct";
	w->base.destroy = comp_window_direct_destroy;
	w->base.flush = comp_window_direct_flush;
	if (c->settings.window_type == WINDOW_DIRECT_NVIDIA) {
		w->base.init = comp_window_direct_init_nvidia;
	} else {
		w->base.init = comp_window_direct_init_randr;
	}
	w->base.init_swapchain = comp_window_direct_init_swapchain;
	w->base.update_window_title = comp_window_direct_update_window_title;
	w->base.c = c;

	return &w->base;
}

static void
comp_window_direct_destroy(struct comp_window *w)
{
	struct comp_window_direct *w_direct = (struct comp_window_direct *)w;
	struct vk_bundle *vk = &w->c->vk;

	for (uint32_t i = 0; i < w_direct->num_displays; i++) {
		struct comp_window_direct_randr_display *d =
		    &w_direct->randr_displays[i];

		if (d->display == VK_NULL_HANDLE) {
			continue;
		}

		vk->vkReleaseDisplayEXT(vk->physical_device, d->display);
		d->display = VK_NULL_HANDLE;
		free(d->name);
	}
	for (uint32_t i = 0; i < w_direct->num_displays; i++) {
		struct comp_window_direct_nvidia_display *d =
		    &w_direct->nv_displays[i];
		d->display = VK_NULL_HANDLE;
		free(d->name);
	}

	if (w_direct->nv_displays != NULL)
		free(w_direct->nv_displays);

	if (w_direct->randr_displays != NULL)
		free(w_direct->randr_displays);

	if (w_direct->dpy) {
		XCloseDisplay(w_direct->dpy);
		w_direct->dpy = NULL;
	}

	free(w);
}

static void
comp_window_direct_list_randr_screens(struct comp_window_direct *w)
{
	for (int i = 0; i < w->num_displays; i++) {
		const struct comp_window_direct_randr_display *d =
		    &w->randr_displays[i];
		COMP_DEBUG(
		    w->base.c, "%d: %s %dx%d@%.2f", i, d->name,
		    d->primary_mode.width, d->primary_mode.height,
		    (double)d->primary_mode.dot_clock /
		        (d->primary_mode.htotal * d->primary_mode.vtotal));
	}
}

static bool
comp_window_direct_init_randr(struct comp_window *w)
{
	// Sanity check.
	if (w->c->vk.instance != VK_NULL_HANDLE) {
		COMP_ERROR(w->c, "Vulkan initialized before RANDR init!");
		return false;
	}

	struct comp_window_direct *w_direct = (struct comp_window_direct *)w;

	if (!comp_window_direct_connect(w_direct)) {
		return false;
	}

	xcb_connection_t *connection = XGetXCBConnection(w_direct->dpy);

	xcb_screen_iterator_t iter =
	    xcb_setup_roots_iterator(xcb_get_setup(connection));

	w_direct->screen = iter.data;

	comp_window_direct_get_randr_outputs(w_direct);

	if (w_direct->num_displays == 0) {
		COMP_ERROR(w->c, "No non-desktop output available.");
		return false;
	}

	if (w->c->settings.display > (int)w_direct->num_displays - 1) {
		COMP_DEBUG(w->c,
		           "Requested display %d, but only %d displays are "
		           "available.",
		           w->c->settings.display,
		           w_direct->num_displays);

		w->c->settings.display = 0;
		struct comp_window_direct_randr_display *d =
		    comp_window_direct_current_randr_display(w_direct);
		COMP_DEBUG(w->c, "Selecting '%s' instead.", d->name);
	}

	if (w->c->settings.display < 0) {
		w->c->settings.display = 0;
		struct comp_window_direct_randr_display *d =
		    comp_window_direct_current_randr_display(w_direct);
		COMP_DEBUG(w->c, "Selecting '%s' first display.", d->name);
	}

	struct comp_window_direct_randr_display *d =
	    comp_window_direct_current_randr_display(w_direct);
	w->c->settings.width = d->primary_mode.width;
	w->c->settings.height = d->primary_mode.height;
	// TODO: size callback
	// set_size_cb(settings->width, settings->height);

	return true;
}

static bool
append_nvidia_entry_on_match(struct comp_window_direct *w,
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
comp_window_direct_init_nvidia(struct comp_window *w)
{
	// Sanity check.
	if (w->c->vk.instance == VK_NULL_HANDLE) {
		COMP_ERROR(w->c, "Vulkan not initialized before NVIDIA init!");
		return false;
	}

	struct comp_window_direct *w_direct = (struct comp_window_direct *)w;

	if (!comp_window_direct_connect(w_direct)) {
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

static struct comp_window_direct_randr_display *
comp_window_direct_current_randr_display(struct comp_window_direct *w)
{
	int index = w->base.c->settings.display;
	if (index == -1)
		index = 0;

	if (w->num_displays <= (uint32_t)index)
		return NULL;

	return &w->randr_displays[index];
}

static struct comp_window_direct_nvidia_display *
comp_window_direct_current_nvidia_display(struct comp_window_direct *w)
{
	int index = w->base.c->settings.display;
	if (index == -1)
		index = 0;

	if (w->num_displays <= (uint32_t)index)
		return NULL;

	return &w->nv_displays[index];
}

static void
comp_window_direct_flush(struct comp_window *w)
{}

static int
choose_best_vk_mode_auto(struct comp_window_direct *w,
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
print_modes(struct comp_window_direct *w,
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
comp_window_direct_get_primary_display_mode(struct comp_window_direct *w,
                                            VkDisplayKHR display)
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
comp_window_direct_create_surface(struct comp_window_direct *w,
                                  VkInstance instance,
                                  VkSurfaceKHR *surface,
                                  uint32_t width,
                                  uint32_t height)
{
	struct comp_window_direct_randr_display *d =
	    comp_window_direct_current_randr_display(w);
	struct comp_window_direct_nvidia_display *nvd =
	    comp_window_direct_current_nvidia_display(w);
	struct vk_bundle *vk = w->base.swapchain.vk;

	VkResult ret = VK_ERROR_INCOMPATIBLE_DISPLAY_KHR;
	VkDisplayKHR _display = VK_NULL_HANDLE;
	if (d) {
		COMP_DEBUG(
		    w->base.c, "Will use display: %s %dx%d@%.2f", d->name,
		    d->primary_mode.width, d->primary_mode.height,
		    (double)d->primary_mode.dot_clock /
		        (d->primary_mode.htotal * d->primary_mode.vtotal));

		d->display =
		    comp_window_direct_get_xlib_randr_output(w, d->output);
		if (d->display == VK_NULL_HANDLE) {
			return VK_ERROR_INITIALIZATION_FAILED;
		}
		ret = comp_window_direct_acquire_xlib_display(w, d->display);
		_display = d->display;
	}

	if (nvd) {
		COMP_DEBUG(w->base.c, "Will use display: %s", nvd->name);
		ret = comp_window_direct_acquire_xlib_display(w, nvd->display);
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
	    comp_window_direct_get_primary_display_mode(w, _display);

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
comp_window_direct_init_swapchain(struct comp_window *w,
                                  uint32_t width,
                                  uint32_t height)
{
	struct comp_window_direct *w_direct = (struct comp_window_direct *)w;

	VkResult ret = comp_window_direct_create_surface(
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
comp_window_direct_connect(struct comp_window_direct *w)
{
	w->dpy = XOpenDisplay(NULL);
	if (w->dpy == NULL) {
		COMP_ERROR(w->base.c, "Could not open X display.");
		return false;
	}
	return true;
}

static VkResult
comp_window_direct_acquire_xlib_display(struct comp_window_direct *w,
                                        VkDisplayKHR display)
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

static VkDisplayKHR
comp_window_direct_get_xlib_randr_output(struct comp_window_direct *w,
                                         RROutput output)
{
	struct vk_bundle *vk = w->base.swapchain.vk;
	VkResult ret;

	VkDisplayKHR display;
	ret = vk->vkGetRandROutputDisplayEXT(
	    w->base.swapchain.vk->physical_device, w->dpy, output, &display);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c, "vkGetRandROutputDisplayEXT: %s",
		           vk_result_string(ret));
		return VK_NULL_HANDLE;
	}

	if (display == VK_NULL_HANDLE) {
		COMP_DEBUG(w->base.c,
		           "vkGetRandROutputDisplayEXT"
		           " returned a null display! %p",
		           (void *)display);
		return VK_NULL_HANDLE;
	}

	return display;
}

static void
append_randr_display(struct comp_window_direct *w,
                     xcb_randr_get_output_info_reply_t *output_reply,
                     xcb_randr_get_screen_resources_reply_t *resources_reply,
                     xcb_randr_output_t xcb_output)
{
	xcb_randr_mode_t *output_modes =
	    xcb_randr_get_output_info_modes(output_reply);

	uint8_t *name = xcb_randr_get_output_info_name(output_reply);
	int name_len = xcb_randr_get_output_info_name_length(output_reply);

	int num_modes = xcb_randr_get_output_info_modes_length(output_reply);
	if (num_modes == 0) {
		COMP_ERROR(w->base.c,
		           "%s does not have any modes "
		           "available. "
		           "Check `xrandr --prop`.",
		           name);
	}

	xcb_randr_mode_info_t *mode_infos =
	    xcb_randr_get_screen_resources_modes(resources_reply);

	int n = xcb_randr_get_screen_resources_modes_length(resources_reply);

	xcb_randr_mode_info_t *mode_info = NULL;
	for (int i = 0; i < n; i++)
		if (mode_infos[i].id == output_modes[0])
			mode_info = &mode_infos[i];

	if (mode_info == NULL)
		COMP_ERROR(w->base.c, "No mode with id %d found??",
		           output_modes[0]);


	struct comp_window_direct_randr_display d = {
	    .name = U_TYPED_ARRAY_CALLOC(char, name_len + 1),
	    .output = xcb_output,
	    .primary_mode = *mode_info,
	    .display = VK_NULL_HANDLE,
	};

	memcpy(d.name, name, name_len);
	d.name[name_len] = '\0';

	w->num_displays += 1;

	U_ARRAY_REALLOC_OR_FREE(w->randr_displays,
	                        struct comp_window_direct_randr_display,
	                        w->num_displays);

	if (w->randr_displays == NULL)
		COMP_ERROR(w->base.c, "Unable to reallocate randr_displays");

	w->randr_displays[w->num_displays - 1] = d;
}

static void
comp_window_direct_get_randr_outputs(struct comp_window_direct *w)
{
	xcb_connection_t *connection = XGetXCBConnection(w->dpy);
	xcb_randr_query_version_cookie_t version_cookie =
	    xcb_randr_query_version(connection, XCB_RANDR_MAJOR_VERSION,
	                            XCB_RANDR_MINOR_VERSION);
	xcb_randr_query_version_reply_t *version_reply =
	    xcb_randr_query_version_reply(connection, version_cookie, NULL);

	if (version_reply == NULL) {
		COMP_ERROR(w->base.c, "Could not get RandR version.");
		return;
	}

	COMP_DEBUG(w->base.c, "RandR version %d.%d",
	           version_reply->major_version, version_reply->minor_version);

	if (version_reply->major_version < 1 ||
	    version_reply->minor_version < 6) {
		COMP_DEBUG(w->base.c, "RandR version below 1.6.");
	}

	free(version_reply);

	xcb_generic_error_t *error = NULL;
	xcb_intern_atom_cookie_t non_desktop_cookie = xcb_intern_atom(
	    connection, 1, strlen("non-desktop"), "non-desktop");
	xcb_intern_atom_reply_t *non_desktop_reply =
	    xcb_intern_atom_reply(connection, non_desktop_cookie, &error);

	if (error != NULL) {
		COMP_ERROR(w->base.c, "xcb_intern_atom_reply returned error %d",
		           error->error_code);
		return;
	}

	if (non_desktop_reply == NULL) {
		COMP_ERROR(w->base.c, "non-desktop reply NULL");
		return;
	}

	if (non_desktop_reply->atom == XCB_NONE) {
		COMP_ERROR(w->base.c, "No output has non-desktop property");
		return;
	}

	xcb_randr_get_screen_resources_cookie_t resources_cookie =
	    xcb_randr_get_screen_resources(connection, w->screen->root);
	xcb_randr_get_screen_resources_reply_t *resources_reply =
	    xcb_randr_get_screen_resources_reply(connection, resources_cookie,
	                                         NULL);
	xcb_randr_output_t *xcb_outputs =
	    xcb_randr_get_screen_resources_outputs(resources_reply);

	int count =
	    xcb_randr_get_screen_resources_outputs_length(resources_reply);
	if (count < 1) {
		COMP_ERROR(w->base.c, "failed to retrieve randr outputs");
	}

	for (int i = 0; i < count; i++) {
		xcb_randr_get_output_info_cookie_t output_cookie =
		    xcb_randr_get_output_info(connection, xcb_outputs[i],
		                              XCB_CURRENT_TIME);
		xcb_randr_get_output_info_reply_t *output_reply =
		    xcb_randr_get_output_info_reply(connection, output_cookie,
		                                    NULL);

		// Only outputs with an available mode should be used
		// (it is possible to see 'ghost' outputs with non-desktop=1).
		if (output_reply->num_modes == 0) {
			free(output_reply);
			continue;
		}

		// Find the first output that has the non-desktop property set.
		xcb_randr_get_output_property_cookie_t prop_cookie;
		prop_cookie = xcb_randr_get_output_property(
		    connection, xcb_outputs[i], non_desktop_reply->atom,
		    XCB_ATOM_NONE, 0, 4, 0, 0);
		xcb_randr_get_output_property_reply_t *prop_reply = NULL;
		prop_reply = xcb_randr_get_output_property_reply(
		    connection, prop_cookie, &error);
		if (error != NULL) {
			COMP_ERROR(w->base.c,
			           "xcb_randr_get_output_property_reply "
			           "returned error %d",
			           error->error_code);
			free(prop_reply);
			continue;
		}

		if (prop_reply == NULL) {
			COMP_ERROR(w->base.c, "property reply == NULL");
			free(prop_reply);
			continue;
		}

		if (prop_reply->type != XCB_ATOM_INTEGER ||
		    prop_reply->num_items != 1 || prop_reply->format != 32) {
			COMP_ERROR(w->base.c, "Invalid non-desktop reply");
			free(prop_reply);
			continue;
		}

		uint8_t non_desktop =
		    *xcb_randr_get_output_property_data(prop_reply);
		if (non_desktop == 1)
			append_randr_display(w, output_reply, resources_reply,
			                     xcb_outputs[i]);

		free(prop_reply);
		free(output_reply);
	}

	free(resources_reply);
}

static void
comp_window_direct_update_window_title(struct comp_window *w, const char *title)
{}
