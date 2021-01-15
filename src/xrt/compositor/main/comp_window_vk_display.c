// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Direct mode on PLATFORM_DISPLAY_KHR code.
 * @author Christoph Haag <christoph.haag@collabora.com>
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
struct vk_display
{
	VkDisplayPropertiesKHR display_properties;
	VkDisplayKHR display;
};

/*!
 * Direct mode "window" into a device, using PLATFORM_DISPLAY_KHR.
 *
 * @implements comp_target_swapchain
 */
struct comp_window_vk_display
{
	struct comp_target_swapchain base;

	struct vk_display *displays;
	uint16_t num_displays;
};

/*
 *
 * Forward declare functions
 *
 */

static void
comp_window_vk_display_destroy(struct comp_target *ct);

static bool
comp_window_vk_display_init(struct comp_target *ct);

static struct vk_display *
comp_window_vk_display_current_display(struct comp_window_vk_display *w);

static bool
comp_window_vk_display_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height);


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
comp_window_vk_display_create(struct comp_compositor *c)
{
	struct comp_window_vk_display *w = U_TYPED_CALLOC(struct comp_window_vk_display);

	comp_target_swapchain_init_set_fnptrs(&w->base);

	w->base.base.name = "VkDisplayKHR";
	w->base.base.destroy = comp_window_vk_display_destroy;
	w->base.base.flush = _flush;
	w->base.base.init_pre_vulkan = comp_window_vk_display_init;
	w->base.base.init_post_vulkan = comp_window_vk_display_init_swapchain;
	w->base.base.set_title = _update_window_title;
	w->base.base.c = c;

	return &w->base.base;
}

static void
comp_window_vk_display_destroy(struct comp_target *ct)
{
	struct comp_window_vk_display *w_direct = (struct comp_window_vk_display *)ct;

	comp_target_swapchain_cleanup(&w_direct->base);

	for (uint32_t i = 0; i < w_direct->num_displays; i++) {
		struct vk_display *d = &w_direct->displays[i];
		d->display = VK_NULL_HANDLE;
	}

	if (w_direct->displays != NULL)
		free(w_direct->displays);

	free(ct);
}

static bool
append_vk_display_entry(struct comp_window_vk_display *w, struct VkDisplayPropertiesKHR *disp)
{
	w->base.base.c->settings.preferred.width = disp->physicalResolution.width;
	w->base.base.c->settings.preferred.height = disp->physicalResolution.height;
	struct vk_display d = {.display_properties = *disp, .display = disp->display};

	w->num_displays += 1;

	U_ARRAY_REALLOC_OR_FREE(w->displays, struct vk_display, w->num_displays);

	if (w->displays == NULL)
		COMP_ERROR(w->base.base.c, "Unable to reallocate vk_display displays");

	w->displays[w->num_displays - 1] = d;

	return true;
}

static void
print_found_displays(struct comp_compositor *c, struct VkDisplayPropertiesKHR *display_props, uint32_t display_count)
{
	COMP_ERROR(c, "== Found Displays ==");
	for (uint32_t i = 0; i < display_count; i++) {
		struct VkDisplayPropertiesKHR *p = &display_props[i];

		COMP_ERROR(c, "[%d] %s with resolution %dx%d, dims %dx%d", i, p->displayName,
		           p->physicalResolution.width, p->physicalResolution.height, p->physicalDimensions.width,
		           p->physicalDimensions.height);
	}
}

static bool
comp_window_vk_display_init(struct comp_target *ct)
{
	struct comp_window_vk_display *w_direct = (struct comp_window_vk_display *)ct;

	// Sanity check.
	if (ct->c->vk.instance == VK_NULL_HANDLE) {
		COMP_ERROR(ct->c, "Vulkan not initialized before vk display init!");
		return false;
	}

	struct vk_bundle comp_vk = ct->c->vk;

	uint32_t display_count;
	if (comp_vk.vkGetPhysicalDeviceDisplayPropertiesKHR(comp_vk.physical_device, &display_count, NULL) !=
	    VK_SUCCESS) {
		COMP_ERROR(ct->c, "Failed to get vulkan display count");
		return false;
	}

	if (display_count == 0) {
		COMP_ERROR(ct->c, "No Vulkan displays found.");
		return false;
	}

	struct VkDisplayPropertiesKHR *display_props = U_TYPED_ARRAY_CALLOC(VkDisplayPropertiesKHR, display_count);

	if (display_props && comp_vk.vkGetPhysicalDeviceDisplayPropertiesKHR(comp_vk.physical_device, &display_count,
	                                                                     display_props) != VK_SUCCESS) {
		COMP_ERROR(ct->c, "Failed to get display properties");
		free(display_props);
		return false;
	}

	if (ct->c->settings.vk_display > (int)display_count) {
		COMP_ERROR(ct->c, "Requested display %d, but only %d found.", ct->c->settings.vk_display,
		           display_count);
		print_found_displays(ct->c, display_props, display_count);
		free(display_props);
		return false;
	}

	append_vk_display_entry(w_direct, &display_props[ct->c->settings.vk_display]);

	struct vk_display *d = comp_window_vk_display_current_display(w_direct);
	if (!d) {
		COMP_ERROR(ct->c, "display not found!");
		print_found_displays(ct->c, display_props, display_count);
		free(display_props);
		return false;
	}

	free(display_props);

	return true;
}

static struct vk_display *
comp_window_vk_display_current_display(struct comp_window_vk_display *w)
{
	int index = w->base.base.c->settings.display;
	if (index == -1)
		index = 0;

	if (w->num_displays <= (uint32_t)index)
		return NULL;

	return &w->displays[index];
}

static bool
init_swapchain(struct comp_target_swapchain *cts, VkDisplayKHR display, uint32_t width, uint32_t height)
{
	VkResult ret;

	ret = comp_window_direct_create_surface(cts, display, width, height);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(cts->base.c, "Failed to create surface! '%s'", vk_result_string(ret));
		return false;
	}

	return true;
}

static bool
comp_window_vk_display_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height)
{
	struct comp_window_vk_display *w_direct = (struct comp_window_vk_display *)ct;

	struct vk_display *d = comp_window_vk_display_current_display(w_direct);
	if (!d) {
		COMP_ERROR(ct->c, "display not found.");
		return false;
	}

	COMP_DEBUG(ct->c, "Will use display: %s", d->display_properties.displayName);

	return init_swapchain(&w_direct->base, d->display, width, height);
}
