// Copyright 2019-2020, Collabora, Ltd.
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

#include "util/u_misc.h"

#include "main/comp_window_direct.h"

#include <inttypes.h>

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

/*!
 * Direct mode "window" into a device, using Vulkan direct mode extension
 * and xcb.
 *
 * @implements comp_target_swapchain
 */
struct comp_window_direct_randr
{
	struct comp_target_swapchain base;

	Display *dpy;
	xcb_screen_t *screen;

	struct comp_window_direct_randr_display *displays;

	uint16_t num_displays;
};



/*
 *
 * Forward declare functions
 *
 */

static void
comp_window_direct_randr_destroy(struct comp_target *ct);

XRT_MAYBE_UNUSED static void
comp_window_direct_randr_list_screens(struct comp_window_direct_randr *w);

static bool
comp_window_direct_randr_init(struct comp_target *ct);

static struct comp_window_direct_randr_display *
comp_window_direct_randr_current_display(struct comp_window_direct_randr *w);

static bool
comp_window_direct_randr_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height);

static VkDisplayKHR
comp_window_direct_randr_get_output(struct comp_window_direct_randr *w, RROutput output);

static void
comp_window_direct_randr_get_outputs(struct comp_window_direct_randr *w);

/*
 *
 * Functions.
 *
 */

static inline struct vk_bundle *
get_vk(struct comp_window_direct_randr *cwdr)
{
	return &cwdr->base.base.c->vk;
}

static void
_flush(struct comp_target *ct)
{
	(void)ct;
}

static void
_update_window_title(struct comp_target *ct, const char *title)
{
	(void)ct;
	(void)title;
}

struct comp_target *
comp_window_direct_randr_create(struct comp_compositor *c)
{
	struct comp_window_direct_randr *w = U_TYPED_CALLOC(struct comp_window_direct_randr);

	comp_target_swapchain_init_set_fnptrs(&w->base);

	w->base.base.name = "direct";
	w->base.base.destroy = comp_window_direct_randr_destroy;
	w->base.base.flush = _flush;
	w->base.base.init_pre_vulkan = comp_window_direct_randr_init;
	w->base.base.init_post_vulkan = comp_window_direct_randr_init_swapchain;
	w->base.base.set_title = _update_window_title;
	w->base.base.c = c;

	return &w->base.base;
}

static void
comp_window_direct_randr_destroy(struct comp_target *ct)
{
	struct comp_window_direct_randr *w_direct = (struct comp_window_direct_randr *)ct;

	comp_target_swapchain_cleanup(&w_direct->base);

	struct vk_bundle *vk = get_vk(w_direct);

	for (uint32_t i = 0; i < w_direct->num_displays; i++) {
		struct comp_window_direct_randr_display *d = &w_direct->displays[i];

		if (d->display == VK_NULL_HANDLE) {
			continue;
		}

		vk->vkReleaseDisplayEXT(vk->physical_device, d->display);
		d->display = VK_NULL_HANDLE;
		free(d->name);
	}

	if (w_direct->displays != NULL)
		free(w_direct->displays);

	if (w_direct->dpy) {
		XCloseDisplay(w_direct->dpy);
		w_direct->dpy = NULL;
	}

	free(ct);
}

static void
comp_window_direct_randr_list_screens(struct comp_window_direct_randr *w)
{
	for (int i = 0; i < w->num_displays; i++) {
		const struct comp_window_direct_randr_display *d = &w->displays[i];
		COMP_DEBUG(w->base.base.c, "%d: %s %dx%d@%.2f", i, d->name, d->primary_mode.width,
		           d->primary_mode.height,
		           (double)d->primary_mode.dot_clock / (d->primary_mode.htotal * d->primary_mode.vtotal));
	}
}

static bool
comp_window_direct_randr_init(struct comp_target *ct)
{
	struct comp_window_direct_randr *w_direct = (struct comp_window_direct_randr *)ct;

	// Sanity check.
	if (ct->c->vk.instance != VK_NULL_HANDLE) {
		COMP_ERROR(ct->c, "Vulkan initialized before RANDR init!");
		return false;
	}


	if (!comp_window_direct_connect(&w_direct->base, &w_direct->dpy)) {
		return false;
	}

	xcb_connection_t *connection = XGetXCBConnection(w_direct->dpy);

	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(connection));

	w_direct->screen = iter.data;

	comp_window_direct_randr_get_outputs(w_direct);

	if (w_direct->num_displays == 0) {
		COMP_ERROR(ct->c, "No non-desktop output available.");
		return false;
	}

	if (ct->c->settings.display > (int)w_direct->num_displays - 1) {
		COMP_DEBUG(ct->c,
		           "Requested display %d, but only %d displays are "
		           "available.",
		           ct->c->settings.display, w_direct->num_displays);

		ct->c->settings.display = 0;
		struct comp_window_direct_randr_display *d = comp_window_direct_randr_current_display(w_direct);
		COMP_DEBUG(ct->c, "Selecting '%s' instead.", d->name);
	}

	if (ct->c->settings.display < 0) {
		ct->c->settings.display = 0;
		struct comp_window_direct_randr_display *d = comp_window_direct_randr_current_display(w_direct);
		COMP_DEBUG(ct->c, "Selecting '%s' first display.", d->name);
	}

	struct comp_window_direct_randr_display *d = comp_window_direct_randr_current_display(w_direct);
	ct->c->settings.preferred.width = d->primary_mode.width;
	ct->c->settings.preferred.height = d->primary_mode.height;

	return true;
}

static struct comp_window_direct_randr_display *
comp_window_direct_randr_current_display(struct comp_window_direct_randr *w)
{
	int index = w->base.base.c->settings.display;
	if (index == -1)
		index = 0;

	if (w->num_displays <= (uint32_t)index)
		return NULL;

	return &w->displays[index];
}

static bool
comp_window_direct_randr_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height)
{
	struct comp_window_direct_randr *w_direct = (struct comp_window_direct_randr *)ct;

	struct comp_window_direct_randr_display *d = comp_window_direct_randr_current_display(w_direct);

	if (!d) {
		COMP_ERROR(ct->c, "RandR could not find any HMDs.");
		return false;
	}

	COMP_DEBUG(ct->c, "Will use display: %s %dx%d@%.2f", d->name, d->primary_mode.width, d->primary_mode.height,
	           (double)d->primary_mode.dot_clock / (d->primary_mode.htotal * d->primary_mode.vtotal));

	d->display = comp_window_direct_randr_get_output(w_direct, d->output);
	if (d->display == VK_NULL_HANDLE) {
		return false;
	}

	return comp_window_direct_init_swapchain(&w_direct->base, w_direct->dpy, d->display, width, height);
}

static VkDisplayKHR
comp_window_direct_randr_get_output(struct comp_window_direct_randr *w, RROutput output)
{
	struct vk_bundle *vk = get_vk(w);
	VkResult ret;

	VkDisplayKHR display;
	ret = vk->vkGetRandROutputDisplayEXT(vk->physical_device, w->dpy, output, &display);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.base.c, "vkGetRandROutputDisplayEXT: %s", vk_result_string(ret));
		return VK_NULL_HANDLE;
	}

	if (display == VK_NULL_HANDLE) {
		COMP_DEBUG(w->base.base.c,
		           "vkGetRandROutputDisplayEXT"
		           " returned a null display! 0x%016" PRIx64,
		           (uint64_t)display);
		return VK_NULL_HANDLE;
	}

	return display;
}

static void
append_randr_display(struct comp_window_direct_randr *w,
                     xcb_randr_get_output_info_reply_t *output_reply,
                     xcb_randr_get_screen_resources_reply_t *resources_reply,
                     xcb_randr_output_t xcb_output)
{
	xcb_randr_mode_t *output_modes = xcb_randr_get_output_info_modes(output_reply);

	uint8_t *name = xcb_randr_get_output_info_name(output_reply);
	int name_len = xcb_randr_get_output_info_name_length(output_reply);

	int num_modes = xcb_randr_get_output_info_modes_length(output_reply);
	if (num_modes == 0) {
		COMP_ERROR(w->base.base.c,
		           "%s does not have any modes "
		           "available. "
		           "Check `xrandr --prop`.",
		           name);
	}

	xcb_randr_mode_info_t *mode_infos = xcb_randr_get_screen_resources_modes(resources_reply);

	int n = xcb_randr_get_screen_resources_modes_length(resources_reply);

	xcb_randr_mode_info_t *mode_info = NULL;
	for (int i = 0; i < n; i++)
		if (mode_infos[i].id == output_modes[0])
			mode_info = &mode_infos[i];

	if (mode_info == NULL)
		COMP_ERROR(w->base.base.c, "No mode with id %d found??", output_modes[0]);


	struct comp_window_direct_randr_display d = {
	    .name = U_TYPED_ARRAY_CALLOC(char, name_len + 1),
	    .output = xcb_output,
	    .primary_mode = *mode_info,
	    .display = VK_NULL_HANDLE,
	};

	memcpy(d.name, name, name_len);
	d.name[name_len] = '\0';

	w->num_displays += 1;

	U_ARRAY_REALLOC_OR_FREE(w->displays, struct comp_window_direct_randr_display, w->num_displays);

	if (w->displays == NULL)
		COMP_ERROR(w->base.base.c, "Unable to reallocate randr_displays");

	w->displays[w->num_displays - 1] = d;
}

static void
comp_window_direct_randr_get_outputs(struct comp_window_direct_randr *w)
{
	struct comp_target *ct = &w->base.base;

	xcb_connection_t *connection = XGetXCBConnection(w->dpy);
	xcb_randr_query_version_cookie_t version_cookie =
	    xcb_randr_query_version(connection, XCB_RANDR_MAJOR_VERSION, XCB_RANDR_MINOR_VERSION);
	xcb_randr_query_version_reply_t *version_reply =
	    xcb_randr_query_version_reply(connection, version_cookie, NULL);

	if (version_reply == NULL) {
		COMP_ERROR(ct->c, "Could not get RandR version.");
		return;
	}

	COMP_DEBUG(ct->c, "RandR version %d.%d", version_reply->major_version, version_reply->minor_version);

	if (version_reply->major_version < 1 || version_reply->minor_version < 6) {
		COMP_DEBUG(ct->c, "RandR version below 1.6.");
	}

	free(version_reply);

	xcb_generic_error_t *error = NULL;
	xcb_intern_atom_cookie_t non_desktop_cookie =
	    xcb_intern_atom(connection, 1, strlen("non-desktop"), "non-desktop");
	xcb_intern_atom_reply_t *non_desktop_reply = xcb_intern_atom_reply(connection, non_desktop_cookie, &error);

	if (error != NULL) {
		COMP_ERROR(ct->c, "xcb_intern_atom_reply returned error %d", error->error_code);
		return;
	}

	if (non_desktop_reply == NULL) {
		COMP_ERROR(ct->c, "non-desktop reply NULL");
		return;
	}

	if (non_desktop_reply->atom == XCB_NONE) {
		COMP_ERROR(ct->c, "No output has non-desktop property");
		return;
	}

	xcb_randr_get_screen_resources_cookie_t resources_cookie =
	    xcb_randr_get_screen_resources(connection, w->screen->root);
	xcb_randr_get_screen_resources_reply_t *resources_reply =
	    xcb_randr_get_screen_resources_reply(connection, resources_cookie, NULL);
	xcb_randr_output_t *xcb_outputs = xcb_randr_get_screen_resources_outputs(resources_reply);

	int count = xcb_randr_get_screen_resources_outputs_length(resources_reply);
	if (count < 1) {
		COMP_ERROR(ct->c, "failed to retrieve randr outputs");
	}

	for (int i = 0; i < count; i++) {
		xcb_randr_get_output_info_cookie_t output_cookie =
		    xcb_randr_get_output_info(connection, xcb_outputs[i], XCB_CURRENT_TIME);
		xcb_randr_get_output_info_reply_t *output_reply =
		    xcb_randr_get_output_info_reply(connection, output_cookie, NULL);

		// Only outputs with an available mode should be used
		// (it is possible to see 'ghost' outputs with non-desktop=1).
		if (output_reply->num_modes == 0) {
			free(output_reply);
			continue;
		}

		// Find the first output that has the non-desktop property set.
		xcb_randr_get_output_property_cookie_t prop_cookie;
		prop_cookie = xcb_randr_get_output_property(connection, xcb_outputs[i], non_desktop_reply->atom,
		                                            XCB_ATOM_NONE, 0, 4, 0, 0);
		xcb_randr_get_output_property_reply_t *prop_reply = NULL;
		prop_reply = xcb_randr_get_output_property_reply(connection, prop_cookie, &error);
		if (error != NULL) {
			COMP_ERROR(ct->c,
			           "xcb_randr_get_output_property_reply "
			           "returned error %d",
			           error->error_code);
			free(prop_reply);
			continue;
		}

		if (prop_reply == NULL) {
			COMP_ERROR(ct->c, "property reply == NULL");
			free(prop_reply);
			continue;
		}

		if (prop_reply->type != XCB_ATOM_INTEGER || prop_reply->num_items != 1 || prop_reply->format != 32) {
			COMP_ERROR(ct->c, "Invalid non-desktop reply");
			free(prop_reply);
			continue;
		}

		uint8_t non_desktop = *xcb_randr_get_output_property_data(prop_reply);
		if (non_desktop == 1)
			append_randr_display(w, output_reply, resources_reply, xcb_outputs[i]);

		free(prop_reply);
		free(output_reply);
	}

	free(resources_reply);
}
