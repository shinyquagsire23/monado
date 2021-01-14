// Copyright 2019-2020, Collabora, Ltd.
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
 *
 * @implements comp_target_swapchain
 */
struct comp_window_direct_nvidia
{
	struct comp_target_swapchain base;

	Display *dpy;
	struct comp_window_direct_nvidia_display *displays;
	uint16_t num_displays;
};

/*
 *
 * Forward declare functions
 *
 */

static void
comp_window_direct_nvidia_destroy(struct comp_target *ct);

static bool
comp_window_direct_nvidia_init(struct comp_target *ct);

static struct comp_window_direct_nvidia_display *
comp_window_direct_nvidia_current_display(struct comp_window_direct_nvidia *w);

static bool
comp_window_direct_nvidia_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height);


/*
 *
 * Functions.
 *
 */

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
comp_window_direct_nvidia_create(struct comp_compositor *c)
{
	struct comp_window_direct_nvidia *w = U_TYPED_CALLOC(struct comp_window_direct_nvidia);

	comp_target_swapchain_init_set_fnptrs(&w->base);

	w->base.base.name = "direct";
	w->base.base.destroy = comp_window_direct_nvidia_destroy;
	w->base.base.flush = _flush;
	w->base.base.init_pre_vulkan = comp_window_direct_nvidia_init;
	w->base.base.init_post_vulkan = comp_window_direct_nvidia_init_swapchain;
	w->base.base.set_title = _update_window_title;
	w->base.base.c = c;

	return &w->base.base;
}

static void
comp_window_direct_nvidia_destroy(struct comp_target *ct)
{
	struct comp_window_direct_nvidia *w_direct = (struct comp_window_direct_nvidia *)ct;

	comp_target_swapchain_cleanup(&w_direct->base);

	for (uint32_t i = 0; i < w_direct->num_displays; i++) {
		struct comp_window_direct_nvidia_display *d = &w_direct->displays[i];
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
	w->base.base.c->settings.preferred.width = disp->physicalResolution.width;
	w->base.base.c->settings.preferred.height = disp->physicalResolution.height;
	struct comp_window_direct_nvidia_display d = {.name = U_TYPED_ARRAY_CALLOC(char, disp_entry_length + 1),
	                                              .display_properties = *disp,
	                                              .display = disp->display};

	memcpy(d.name, disp->displayName, disp_entry_length);
	d.name[disp_entry_length] = '\0';

	w->num_displays += 1;

	U_ARRAY_REALLOC_OR_FREE(w->displays, struct comp_window_direct_nvidia_display, w->num_displays);

	if (w->displays == NULL)
		COMP_ERROR(w->base.base.c, "Unable to reallocate randr_displays");

	w->displays[w->num_displays - 1] = d;

	return true;
}

static bool
comp_window_direct_nvidia_init(struct comp_target *ct)
{
	struct comp_window_direct_nvidia *w_direct = (struct comp_window_direct_nvidia *)ct;

	// Sanity check.
	if (ct->c->vk.instance == VK_NULL_HANDLE) {
		COMP_ERROR(ct->c, "Vulkan not initialized before NVIDIA init!");
		return false;
	}


	if (!comp_window_direct_connect(&w_direct->base, &w_direct->dpy)) {
		return false;
	}

	struct vk_bundle comp_vk = ct->c->vk;

	// find our display using nvidia whitelist, enumerate its modes, and
	// pick the best one get a list of attached displays
	uint32_t display_count;
	if (comp_vk.vkGetPhysicalDeviceDisplayPropertiesKHR(comp_vk.physical_device, &display_count, NULL) !=
	    VK_SUCCESS) {
		COMP_ERROR(ct->c, "Failed to get vulkan display count");
		return false;
	}

	if (display_count == 0) {
		COMP_ERROR(ct->c, "NVIDIA: No Vulkan displays found.");
		return false;
	}

	struct VkDisplayPropertiesKHR *display_props = U_TYPED_ARRAY_CALLOC(VkDisplayPropertiesKHR, display_count);

	if (display_props && comp_vk.vkGetPhysicalDeviceDisplayPropertiesKHR(comp_vk.physical_device, &display_count,
	                                                                     display_props) != VK_SUCCESS) {
		COMP_ERROR(ct->c, "Failed to get display properties");
		free(display_props);
		return false;
	}

	// TODO: what if we have multiple whitelisted HMD displays connected?
	for (uint32_t i = 0; i < display_count; i++) {
		struct VkDisplayPropertiesKHR disp = *(display_props + i);

		if (ct->c->settings.nvidia_display) {
			append_nvidia_entry_on_match(w_direct, ct->c->settings.nvidia_display, &disp);
		}

		// check this display against our whitelist
		for (uint32_t j = 0; j < ARRAY_SIZE(NV_DIRECT_WHITELIST); j++)
			if (append_nvidia_entry_on_match(w_direct, NV_DIRECT_WHITELIST[j], &disp))
				break;
	}

	free(display_props);

	return true;
}

static struct comp_window_direct_nvidia_display *
comp_window_direct_nvidia_current_display(struct comp_window_direct_nvidia *w)
{
	int index = w->base.base.c->settings.display;
	if (index == -1)
		index = 0;

	if (w->num_displays <= (uint32_t)index)
		return NULL;

	return &w->displays[index];
}

static bool
comp_window_direct_nvidia_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height)
{
	struct comp_window_direct_nvidia *w_direct = (struct comp_window_direct_nvidia *)ct;

	struct comp_window_direct_nvidia_display *d = comp_window_direct_nvidia_current_display(w_direct);
	if (!d) {
		COMP_ERROR(ct->c, "NVIDIA could not find any HMDs.");
		return false;
	}

	COMP_DEBUG(ct->c, "Will use display: %s", d->name);

	return comp_window_direct_init_swapchain(&w_direct->base, w_direct->dpy, d->display, width, height);
}
