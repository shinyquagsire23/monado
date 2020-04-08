// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Direct mode window code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include "util/u_misc.h"

#include "main/comp_window_direct.h"

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
	struct comp_window_direct_nvidia_display *displays;
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

static bool
comp_window_direct_nvidia_init_swapchain(struct comp_window *w,
                                         uint32_t width,
                                         uint32_t height);

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
		    &w_direct->displays[i];
		d->display = VK_NULL_HANDLE;
		free(d->name);
	}

	if (w_direct->displays != NULL)
		free(w_direct->displays);

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

	U_ARRAY_REALLOC_OR_FREE(w->displays,
	                        struct comp_window_direct_nvidia_display,
	                        w->num_displays);

	if (w->displays == NULL)
		COMP_ERROR(w->base.c, "Unable to reallocate randr_displays");

	w->displays[w->num_displays - 1] = d;

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

	if (!comp_window_direct_connect(w, &w_direct->dpy)) {
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

	return &w->displays[index];
}

static bool
comp_window_direct_nvidia_init_swapchain(struct comp_window *w,
                                         uint32_t width,
                                         uint32_t height)
{
	struct comp_window_direct_nvidia *w_direct =
	    (struct comp_window_direct_nvidia *)w;

	struct comp_window_direct_nvidia_display *d =
	    comp_window_direct_nvidia_current_display(w_direct);
	if (!d) {
		COMP_ERROR(w->c, "NVIDIA could not find any HMDs.");
		return false;
	}

	COMP_DEBUG(w->c, "Will use display: %s", d->name);

	return comp_window_direct_init_swapchain(w, w_direct->dpy, d->display,
	                                         width, height);
}
