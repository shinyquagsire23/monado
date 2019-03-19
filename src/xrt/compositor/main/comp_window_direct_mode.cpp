// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Direct mode window code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp
 */

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <X11/extensions/Xrandr.h>

#include <map>
#include <string>
#include <vector>
#include <utility>
#include <cstring>

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
struct comp_window_direct_display
{
	std::string name;
	xcb_randr_output_t output;
	xcb_randr_mode_info_t primary_mode;
	VkDisplayKHR display;
};

/*!
 * Direct mode "window" into a device, using Vulkan direct mode extension
 * and xcb.
 */
struct comp_window_direct
{
	struct comp_window base = comp_window();

	Display* dpy = nullptr;
	xcb_connection_t* connection = nullptr;
	xcb_screen_t* screen = nullptr;

	std::map<uint32_t, xcb_randr_mode_info_t> modes = {};

	std::vector<comp_window_direct_display> displays = {};
};


/*
 *
 * Pre decalre functions
 *
 */

static void
comp_window_direct_destroy(struct comp_window* w);

XRT_MAYBE_UNUSED static void
comp_window_direct_list_screens(struct comp_window_direct* w);

static bool
comp_window_direct_init(struct comp_window* w);

static comp_window_direct_display*
comp_window_direct_current_display(struct comp_window_direct* w);

static void
comp_window_direct_flush(struct comp_window* w);

static VkDisplayModeKHR
comp_window_direct_get_primary_display_mode(struct comp_window_direct* w,
                                            VkDisplayKHR display);

static VkDisplayPlaneAlphaFlagBitsKHR
choose_alpha_mode(VkDisplayPlaneAlphaFlagsKHR flags);

static VkResult
comp_window_direct_create_surface(struct comp_window_direct* w,
                                  VkInstance instance,
                                  VkSurfaceKHR* surface,
                                  uint32_t width,
                                  uint32_t height);

static bool
comp_window_direct_init_swapchain(struct comp_window* w,
                                  uint32_t width,
                                  uint32_t height);

static int
comp_window_direct_connect(struct comp_window_direct* w);

static VkResult
comp_window_direct_aquire_xlib_display(struct comp_window_direct* w,
                                       VkDisplayKHR display);

static VkDisplayKHR
comp_window_direct_get_xlib_randr_output(struct comp_window_direct* w,
                                         RROutput output);

static void
comp_window_direct_enumerate_modes(
    struct comp_window_direct* w,
    xcb_randr_get_screen_resources_reply_t* resources_reply);

static void
comp_window_direct_get_randr_outputs(struct comp_window_direct* w);

static void
comp_window_direct_update_window_title(struct comp_window* w,
                                       const char* title);


/*
 *
 * Functions.
 *
 */

extern "C" struct comp_window*
comp_window_direct_create(struct comp_compositor* c)
{
	auto w = new comp_window_direct();

	w->base.name = "direct";
	w->base.destroy = comp_window_direct_destroy;
	w->base.flush = comp_window_direct_flush;
	w->base.init = comp_window_direct_init;
	w->base.init_swapchain = comp_window_direct_init_swapchain;
	w->base.update_window_title = comp_window_direct_update_window_title;
	w->base.c = c;
	w->modes.clear();

	return &w->base;
}

static void
comp_window_direct_destroy(struct comp_window* w)
{
	comp_window_direct* w_direct = (comp_window_direct*)w;
	vk_bundle* vk = &w->c->vk;

	for (uint32_t i = 0; i < w_direct->displays.size(); i++) {
		comp_window_direct_display* d = &w_direct->displays[i];

		if (d->display == VK_NULL_HANDLE) {
			continue;
		}

		vk->vkReleaseDisplayEXT(vk->physical_device, d->display);
		d->display = VK_NULL_HANDLE;
	}

	if (w_direct->connection) {
		xcb_disconnect(w_direct->connection);
		w_direct->connection = nullptr;
	}

	delete w;
}

static void
comp_window_direct_list_screens(struct comp_window_direct* w)
{
	int display_i = 0;
	for (comp_window_direct_display d : w->displays) {
		COMP_DEBUG(w->base.c, "%d: %s %dx%d@%.2f", display_i,
		           d.name.c_str(), d.primary_mode.width,
		           d.primary_mode.height,
		           (double)d.primary_mode.dot_clock /
		               (d.primary_mode.htotal * d.primary_mode.vtotal));
		display_i++;
	}
}

static bool
comp_window_direct_init(struct comp_window* w)
{
	comp_window_direct* w_direct = (comp_window_direct*)w;

	if (!comp_window_direct_connect(w_direct)) {
		return false;
	}

	xcb_screen_iterator_t iter =
	    xcb_setup_roots_iterator(xcb_get_setup(w_direct->connection));

	w_direct->screen = iter.data;

	comp_window_direct_get_randr_outputs(w_direct);

	if (w_direct->displays.size() < 1) {
		COMP_ERROR(w->c, "No non-desktop output available.");
		return false;
	}

	if (w->c->settings.display > (int)w_direct->displays.size() - 1) {
		COMP_DEBUG(w->c,
		           "Requested display %d, but only %d displays are "
		           "available.",
		           w->c->settings.display,
		           (int)w_direct->displays.size());

		w->c->settings.display = 0;
		comp_window_direct_display* d =
		    comp_window_direct_current_display(w_direct);
		COMP_DEBUG(w->c, "Selecting '%s' instead.", d->name.c_str());
	}

	if (w->c->settings.display < 0) {
		w->c->settings.display = 0;
		comp_window_direct_display* d =
		    comp_window_direct_current_display(w_direct);
		COMP_DEBUG(w->c, "Selecting '%s' first display.",
		           d->name.c_str());
	}

	comp_window_direct_display* d =
	    comp_window_direct_current_display(w_direct);
	w->c->settings.width = d->primary_mode.width;
	w->c->settings.height = d->primary_mode.height;
	// TODO: size callback
	// set_size_cb(settings->width, settings->height);

	return true;
}

static comp_window_direct_display*
comp_window_direct_current_display(struct comp_window_direct* w)
{
	uint32_t index = w->base.c->settings.display;
	if (index == (uint32_t)-1) {
		index = 0;
	}

	if (w->displays.size() <= index) {
		return nullptr;
	}

	return &w->displays[index];
}

static void
comp_window_direct_flush(struct comp_window* w)
{}

static VkDisplayModeKHR
comp_window_direct_get_primary_display_mode(struct comp_window_direct* w,
                                            VkDisplayKHR display)
{
	struct vk_bundle* vk = w->base.swapchain.vk;
	uint32_t mode_count;
	VkResult ret;

	ret = vk->vkGetDisplayModePropertiesKHR(
	    w->base.swapchain.vk->physical_device, display, &mode_count,
	    nullptr);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c, "vkGetDisplayModePropertiesKHR: %s",
		           vk_result_string(ret));
		return nullptr;
	}

	COMP_DEBUG(w->base.c, "Found %d modes", mode_count);

	VkDisplayModePropertiesKHR* mode_properties;
	mode_properties = new VkDisplayModePropertiesKHR[mode_count];
	ret = vk->vkGetDisplayModePropertiesKHR(
	    w->base.swapchain.vk->physical_device, display, &mode_count,
	    mode_properties);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c, "vkGetDisplayModePropertiesKHR: %s",
		           vk_result_string(ret));
		delete[] mode_properties;
		return nullptr;
	}

	VkDisplayModePropertiesKHR props = mode_properties[0];

	COMP_DEBUG(w->base.c, "found display mode %d %d",
	           props.parameters.visibleRegion.width,
	           props.parameters.visibleRegion.height);

	delete[] mode_properties;

	return props.displayMode;
}

static VkDisplayPlaneAlphaFlagBitsKHR
choose_alpha_mode(VkDisplayPlaneAlphaFlagsKHR flags)
{
	if (flags & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR) {
		return VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR;
	} else if (flags & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR) {
		return VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR;
	} else {
		return VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
	}
}

static VkResult
comp_window_direct_create_surface(struct comp_window_direct* w,
                                  VkInstance instance,
                                  VkSurfaceKHR* surface,
                                  uint32_t width,
                                  uint32_t height)
{
	comp_window_direct_display* d = comp_window_direct_current_display(w);
	struct vk_bundle* vk = w->base.swapchain.vk;
	VkResult ret;

	COMP_DEBUG(w->base.c, "Will use display: %s %dx%d@%.2f",
	           d->name.c_str(), d->primary_mode.width,
	           d->primary_mode.height,
	           (double)d->primary_mode.dot_clock /
	               (d->primary_mode.htotal * d->primary_mode.vtotal));

	d->display = comp_window_direct_get_xlib_randr_output(w, d->output);
	if (d->display == VK_NULL_HANDLE) {
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	ret = comp_window_direct_aquire_xlib_display(w, d->display);
	if (ret != VK_SUCCESS) {
		return ret;
	}


	// Get plane properties
	uint32_t plane_property_count;
	ret = vk->vkGetPhysicalDeviceDisplayPlanePropertiesKHR(
	    w->base.swapchain.vk->physical_device, &plane_property_count,
	    nullptr);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c,
		           "vkGetPhysicalDeviceDisplayPlanePropertiesKHR: %s",
		           vk_result_string(ret));
		return ret;
	}

	COMP_DEBUG(w->base.c, "Found %d plane properites.",
	           plane_property_count);

	VkDisplayPlanePropertiesKHR* plane_properties =
	    new VkDisplayPlanePropertiesKHR[plane_property_count];

	ret = vk->vkGetPhysicalDeviceDisplayPlanePropertiesKHR(
	    w->base.swapchain.vk->physical_device, &plane_property_count,
	    plane_properties);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c,
		           "vkGetPhysicalDeviceDisplayPlanePropertiesKHR: %s",
		           vk_result_string(ret));
		delete[] plane_properties;
		return ret;
	}

	uint32_t plane_index = 0;

	VkDisplayModeKHR display_mode =
	    comp_window_direct_get_primary_display_mode(w, d->display);

	VkDisplayPlaneCapabilitiesKHR plane_caps;
	vk->vkGetDisplayPlaneCapabilitiesKHR(
	    w->base.swapchain.vk->physical_device, display_mode, plane_index,
	    &plane_caps);

	VkDisplaySurfaceCreateInfoKHR surface_info = {
	    .sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
	    .pNext = nullptr,
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
	    instance, &surface_info, nullptr, surface);

	delete[] plane_properties;

	return result;
}

static bool
comp_window_direct_init_swapchain(struct comp_window* w,
                                  uint32_t width,
                                  uint32_t height)
{
	comp_window_direct* w_direct = (comp_window_direct*)w;
	VkResult ret;


	ret = comp_window_direct_create_surface(
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
comp_window_direct_connect(struct comp_window_direct* w)
{
	w->dpy = XOpenDisplay(nullptr);
	if (w->dpy == nullptr) {
		COMP_ERROR(w->base.c, "Could not open X display.");
		return false;
	}

	//! @todo only open one connection and use XGetXCBConnection.
	w->connection = xcb_connect(nullptr, nullptr);
	return !xcb_connection_has_error(w->connection);
}

static VkResult
comp_window_direct_aquire_xlib_display(struct comp_window_direct* w,
                                       VkDisplayKHR display)
{
	struct vk_bundle* vk = w->base.swapchain.vk;
	VkResult ret;

	ret = vk->vkAcquireXlibDisplayEXT(w->base.swapchain.vk->physical_device,
	                                  w->dpy, display);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c, "vkAcquireXlibDisplayEXT: %s (%p)",
		           vk_result_string(ret), (void*)display);
	}
	return ret;
}

static VkDisplayKHR
comp_window_direct_get_xlib_randr_output(struct comp_window_direct* w,
                                         RROutput output)
{
	struct vk_bundle* vk = w->base.swapchain.vk;
	VkResult ret;

	VkDisplayKHR display;
	ret = vk->vkGetRandROutputDisplayEXT(
	    w->base.swapchain.vk->physical_device, w->dpy, output, &display);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c, "vkGetRandROutputDisplayEXT: %s",
		           vk_result_string(ret));
		return nullptr;
	}

	if (display == VK_NULL_HANDLE) {
		COMP_DEBUG(w->base.c,
		           "vkGetRandROutputDisplayEXT"
		           " returned a null display! %p",
		           display);
		return VK_NULL_HANDLE;
	}

	return display;
}

static void
comp_window_direct_enumerate_modes(
    struct comp_window_direct* w,
    xcb_randr_get_screen_resources_reply_t* resources_reply)
{
	xcb_randr_mode_info_t* mode_infos =
	    xcb_randr_get_screen_resources_modes(resources_reply);

	int n = xcb_randr_get_screen_resources_modes_length(resources_reply);
	for (int i = 0; i < n; i++)
		w->modes.insert(std::pair<uint32_t, xcb_randr_mode_info_t>(
		    mode_infos[i].id, mode_infos[i]));
}

static void
comp_window_direct_get_randr_outputs(struct comp_window_direct* w)
{
	xcb_randr_query_version_cookie_t version_cookie =
	    xcb_randr_query_version(w->connection, XCB_RANDR_MAJOR_VERSION,
	                            XCB_RANDR_MINOR_VERSION);
	xcb_randr_query_version_reply_t* version_reply =
	    xcb_randr_query_version_reply(w->connection, version_cookie, NULL);

	if (!version_reply) {
		COMP_ERROR(w->base.c, "Could not get RandR version.");
		return;
	}

	COMP_DEBUG(w->base.c, "RandR version %d.%d",
	           version_reply->major_version, version_reply->minor_version);

	if (version_reply->major_version < 1 ||
	    version_reply->minor_version < 6) {
		COMP_DEBUG(w->base.c, "RandR version below 1.6.");
	}

	xcb_generic_error_t* error = nullptr;
	xcb_intern_atom_cookie_t non_desktop_cookie = xcb_intern_atom(
	    w->connection, 1, strlen("non-desktop"), "non-desktop");
	xcb_intern_atom_reply_t* non_desktop_reply =
	    xcb_intern_atom_reply(w->connection, non_desktop_cookie, &error);

	if (error != nullptr) {
		COMP_ERROR(w->base.c, "xcb_intern_atom_reply returned error %d",
		           error->error_code);
		return;
	}

	if (non_desktop_reply == nullptr) {
		COMP_ERROR(w->base.c, "non-desktop reply nullptr");
		return;
	}

	if (non_desktop_reply->atom == XCB_NONE) {
		COMP_ERROR(w->base.c, "No output has non-desktop property");
		return;
	}

	xcb_randr_get_screen_resources_cookie_t resources_cookie =
	    xcb_randr_get_screen_resources(w->connection, w->screen->root);
	xcb_randr_get_screen_resources_reply_t* resources_reply =
	    xcb_randr_get_screen_resources_reply(w->connection,
	                                         resources_cookie, nullptr);
	xcb_randr_output_t* xcb_outputs =
	    xcb_randr_get_screen_resources_outputs(resources_reply);

	comp_window_direct_enumerate_modes(w, resources_reply);

	int count =
	    xcb_randr_get_screen_resources_outputs_length(resources_reply);
	if (count < 1) {
		COMP_ERROR(w->base.c, "failed to retrieve randr outputs");
	}

	for (int i = 0; i < count; i++) {
		xcb_randr_get_output_info_cookie_t output_cookie =
		    xcb_randr_get_output_info(w->connection, xcb_outputs[i],
		                              XCB_CURRENT_TIME);
		xcb_randr_get_output_info_reply_t* output_reply =
		    xcb_randr_get_output_info_reply(w->connection,
		                                    output_cookie, nullptr);

		// Only outputs with an available mode should be used
		// (it is possible to see 'ghost' outputs with non-desktop=1).
		if (output_reply->num_modes == 0) {
			continue;
		}

		uint8_t* name = xcb_randr_get_output_info_name(output_reply);
		int name_len =
		    xcb_randr_get_output_info_name_length(output_reply);

		char* name_str = (char*)malloc(name_len + 1);
		memcpy(name_str, name, name_len);
		name_str[name_len] = '\0';

		// Find the first output that has the non-desktop property set.
		xcb_randr_get_output_property_cookie_t prop_cookie;
		prop_cookie = xcb_randr_get_output_property(
		    w->connection, xcb_outputs[i], non_desktop_reply->atom,
		    XCB_ATOM_NONE, 0, 4, 0, 0);
		xcb_randr_get_output_property_reply_t* prop_reply = nullptr;
		prop_reply = xcb_randr_get_output_property_reply(
		    w->connection, prop_cookie, &error);
		if (error != nullptr) {
			COMP_ERROR(w->base.c,
			           "xcb_randr_get_output_property_reply "
			           "returned error %d",
			           error->error_code);
			free(name_str);
			continue;
		}

		if (prop_reply == nullptr) {
			COMP_ERROR(w->base.c, "property reply == nullptr");
			free(name_str);
			continue;
		}

		if (prop_reply->type != XCB_ATOM_INTEGER ||
		    prop_reply->num_items != 1 || prop_reply->format != 32) {
			COMP_ERROR(w->base.c, "Invalid non-desktop reply");
			free(name_str);
			continue;
		}

		uint8_t non_desktop =
		    *xcb_randr_get_output_property_data(prop_reply);
		if (non_desktop == 1) {
			xcb_randr_mode_t* output_modes =
			    xcb_randr_get_output_info_modes(output_reply);

			int num_modes = xcb_randr_get_output_info_modes_length(
			    output_reply);
			if (num_modes == 0) {
				COMP_ERROR(w->base.c,
				           "%s does not have any modes "
				           "available. "
				           "Check `xrandr --prop`.",
				           name_str);
			}

			if (!w->modes.count(output_modes[0])) {
				COMP_ERROR(w->base.c,
				           "No mode with id %d found??",
				           output_modes[0]);
			}

			comp_window_direct_display d = {
			    .name = std::string(name_str),
			    .output = xcb_outputs[i],
			    .primary_mode = w->modes.at(output_modes[0]),
			    .display = VK_NULL_HANDLE,
			};

			w->displays.push_back(d);
		}

		free(name_str);
	}
}

static void
comp_window_direct_update_window_title(struct comp_window* w, const char* title)
{}

#endif
