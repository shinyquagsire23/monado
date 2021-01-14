// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  XCB window code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include <xcb/xcb.h>
#include <xcb/randr.h>

#include "util/u_misc.h"
#include "xrt/xrt_compiler.h"
#include "main/comp_window.h"


/*
 *
 * Private structs.
 *
 */

/*!
 * Xcb display, xrandr output.
 */
struct comp_window_xcb_display
{
	char *name;
	struct
	{
		int16_t x;
		int16_t y;
	} position;

	struct
	{
		uint16_t width;
		uint16_t height;
	} size;
};

/*!
 * A xcb connection and window.
 *
 * @implements comp_target_swapchain
 */
struct comp_window_xcb
{
	struct comp_target_swapchain base;

	xcb_connection_t *connection;
	xcb_window_t window;
	xcb_screen_t *screen;

	xcb_atom_t atom_wm_protocols;
	xcb_atom_t atom_wm_delete_window;

	struct comp_window_xcb_display *displays;
	uint16_t num_displays;
};


/*
 *
 * Pre declare functions.
 *
 */

static void
comp_window_xcb_destroy(struct comp_target *ct);

static void
comp_window_xcb_flush(struct comp_target *ct);

XRT_MAYBE_UNUSED static void
comp_window_xcb_list_screens(struct comp_window_xcb *w, xcb_screen_t *screen);

static bool
comp_window_xcb_init(struct comp_target *ct);

static struct comp_window_xcb_display *
comp_window_xcb_current_display(struct comp_window_xcb *w);

static bool
comp_window_xcb_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height);

static int
comp_window_xcb_connect(struct comp_window_xcb *w);

static void
comp_window_xcb_create_window(struct comp_window_xcb *w, uint32_t width, uint32_t height);

static void
comp_window_xcb_get_randr_outputs(struct comp_window_xcb *w);

static void
comp_window_xcb_connect_delete_event(struct comp_window_xcb *w);

static void
comp_window_xcb_set_full_screen(struct comp_window_xcb *w);

static xcb_atom_t
comp_window_xcb_get_atom(struct comp_window_xcb *w, const char *name);

static VkResult
comp_window_xcb_create_surface(struct comp_window_xcb *w, VkSurfaceKHR *surface);

static void
comp_window_xcb_update_window_title(struct comp_target *ct, const char *title);


/*
 *
 * Functions.
 *
 */

static inline struct vk_bundle *
get_vk(struct comp_window_xcb *cwx)
{
	return &cwx->base.base.c->vk;
}

struct comp_target *
comp_window_xcb_create(struct comp_compositor *c)
{
	struct comp_window_xcb *w = U_TYPED_CALLOC(struct comp_window_xcb);

	comp_target_swapchain_init_set_fnptrs(&w->base);

	w->base.base.name = "xcb";
	w->base.base.destroy = comp_window_xcb_destroy;
	w->base.base.flush = comp_window_xcb_flush;
	w->base.base.init_pre_vulkan = comp_window_xcb_init;
	w->base.base.init_post_vulkan = comp_window_xcb_init_swapchain;
	w->base.base.set_title = comp_window_xcb_update_window_title;
	w->base.base.c = c;

	return &w->base.base;
}

static void
comp_window_xcb_destroy(struct comp_target *ct)
{
	struct comp_window_xcb *w_xcb = (struct comp_window_xcb *)ct;

	comp_target_swapchain_cleanup(&w_xcb->base);

	xcb_destroy_window(w_xcb->connection, w_xcb->window);
	xcb_disconnect(w_xcb->connection);

	for (uint16_t i = 0; i > w_xcb->num_displays; i++)
		free(w_xcb->displays[i].name);

	free(w_xcb->displays);

	free(ct);
}

static void
comp_window_xcb_list_screens(struct comp_window_xcb *w, xcb_screen_t *screen)
{
	COMP_DEBUG(w->base.base.c, "Screen 0 %dx%d", screen->width_in_pixels, screen->height_in_pixels);
	comp_window_xcb_get_randr_outputs(w);

	for (uint16_t i = 0; i < w->num_displays; i++) {
		struct comp_window_xcb_display *d = &w->displays[i];
		COMP_DEBUG(w->base.base.c, "%d: %s %dx%d [%d, %d]", i, d->name, d->size.width, d->size.height,
		           d->position.x, d->position.y);
	}
}

static bool
comp_window_xcb_init(struct comp_target *ct)
{
	struct comp_window_xcb *w_xcb = (struct comp_window_xcb *)ct;

	if (!comp_window_xcb_connect(w_xcb)) {
		return false;
	}

	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(w_xcb->connection));

	w_xcb->screen = iter.data;

	if (ct->c->settings.fullscreen) {
		comp_window_xcb_get_randr_outputs(w_xcb);

		if (ct->c->settings.display > (int)w_xcb->num_displays - 1) {
			COMP_DEBUG(ct->c,
			           "Requested display %d, but only %d "
			           "displays are available.",
			           ct->c->settings.display, w_xcb->num_displays);

			ct->c->settings.display = 0;
			struct comp_window_xcb_display *d = comp_window_xcb_current_display(w_xcb);
			COMP_DEBUG(ct->c, "Selecting '%s' instead.", d->name);
		}

		if (ct->c->settings.display == -1)
			ct->c->settings.display = 0;

		struct comp_window_xcb_display *d = comp_window_xcb_current_display(w_xcb);
		ct->c->settings.preferred.width = d->size.width;
		ct->c->settings.preferred.height = d->size.height;
		// TODO: size cb
		// set_size_cb(settings->width, settings->height);
	}

	comp_window_xcb_create_window(w_xcb, ct->c->settings.preferred.width, ct->c->settings.preferred.height);

	comp_window_xcb_connect_delete_event(w_xcb);

	if (ct->c->settings.fullscreen)
		comp_window_xcb_set_full_screen(w_xcb);

	xcb_map_window(w_xcb->connection, w_xcb->window);

	return true;
}

static struct comp_window_xcb_display *
comp_window_xcb_current_display(struct comp_window_xcb *w)
{
	return &w->displays[w->base.base.c->settings.display];
}

static void
comp_window_xcb_flush(struct comp_target *ct)
{
	(void)ct;
}

static bool
comp_window_xcb_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height)
{
	struct comp_window_xcb *w_xcb = (struct comp_window_xcb *)ct;
	VkResult ret;

	ret = comp_window_xcb_create_surface(w_xcb, &w_xcb->base.surface.handle);
	if (ret != VK_SUCCESS) {
		return false;
	}

	return true;
}

static int
comp_window_xcb_connect(struct comp_window_xcb *w)
{
	w->connection = xcb_connect(NULL, NULL);
	return !xcb_connection_has_error(w->connection);
}

static void
comp_window_xcb_create_window(struct comp_window_xcb *w, uint32_t width, uint32_t height)
{
	w->window = xcb_generate_id(w->connection);

	int x = 0;
	int y = 0;

	if (w->base.base.c->settings.fullscreen) {
		x = comp_window_xcb_current_display(w)->position.x;
		y = comp_window_xcb_current_display(w)->position.y;
	}

	uint32_t value_list = XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	xcb_create_window(w->connection, XCB_COPY_FROM_PARENT, w->window, w->screen->root, x, y, width, height, 0,
	                  XCB_WINDOW_CLASS_INPUT_OUTPUT, w->screen->root_visual, XCB_CW_EVENT_MASK, &value_list);
}

static void
comp_window_xcb_get_randr_outputs(struct comp_window_xcb *w)
{
	xcb_randr_get_screen_resources_cookie_t resources_cookie =
	    xcb_randr_get_screen_resources(w->connection, w->screen->root);
	xcb_randr_get_screen_resources_reply_t *resources_reply =
	    xcb_randr_get_screen_resources_reply(w->connection, resources_cookie, NULL);
	xcb_randr_output_t *xcb_outputs = xcb_randr_get_screen_resources_outputs(resources_reply);

	w->num_displays = xcb_randr_get_screen_resources_outputs_length(resources_reply);
	if (w->num_displays < 1)
		COMP_ERROR(w->base.base.c, "Failed to retrieve randr outputs");

	w->displays = calloc(w->num_displays, sizeof(struct comp_window_xcb_display));

	for (int i = 0; i < w->num_displays; i++) {
		xcb_randr_get_output_info_cookie_t output_cookie =
		    xcb_randr_get_output_info(w->connection, xcb_outputs[i], XCB_CURRENT_TIME);
		xcb_randr_get_output_info_reply_t *output_reply =
		    xcb_randr_get_output_info_reply(w->connection, output_cookie, NULL);

		if (output_reply->connection != XCB_RANDR_CONNECTION_CONNECTED || output_reply->crtc == XCB_NONE) {
			free(output_reply);
			continue;
		}

		xcb_randr_get_crtc_info_cookie_t crtc_cookie =
		    xcb_randr_get_crtc_info(w->connection, output_reply->crtc, XCB_CURRENT_TIME);
		xcb_randr_get_crtc_info_reply_t *crtc_reply =
		    xcb_randr_get_crtc_info_reply(w->connection, crtc_cookie, NULL);

		uint8_t *name = xcb_randr_get_output_info_name(output_reply);
		int name_len = xcb_randr_get_output_info_name_length(output_reply);

		w->displays[i] = (struct comp_window_xcb_display){
		    .name = U_TYPED_ARRAY_CALLOC(char, name_len + 1),
		    .position = {crtc_reply->x, crtc_reply->y},
		    .size = {crtc_reply->width, crtc_reply->height},
		};
		memcpy(w->displays[i].name, name, name_len);
		w->displays[i].name[name_len] = '\0';

		free(crtc_reply);
		free(output_reply);
	}

	free(resources_reply);
}

static void
comp_window_xcb_connect_delete_event(struct comp_window_xcb *w)
{
	w->atom_wm_protocols = comp_window_xcb_get_atom(w, "WM_PROTOCOLS");
	w->atom_wm_delete_window = comp_window_xcb_get_atom(w, "WM_DELETE_WINDOW");
	xcb_change_property(w->connection, XCB_PROP_MODE_REPLACE, w->window, w->atom_wm_protocols, XCB_ATOM_ATOM, 32, 1,
	                    &w->atom_wm_delete_window);
}

static void
comp_window_xcb_set_full_screen(struct comp_window_xcb *w)
{
	xcb_atom_t atom_wm_state = comp_window_xcb_get_atom(w, "_NET_WM_STATE");
	xcb_atom_t atom_wm_fullscreen = comp_window_xcb_get_atom(w, "_NET_WM_STATE_FULLSCREEN");
	xcb_change_property(w->connection, XCB_PROP_MODE_REPLACE, w->window, atom_wm_state, XCB_ATOM_ATOM, 32, 1,
	                    &atom_wm_fullscreen);
}

static xcb_atom_t
comp_window_xcb_get_atom(struct comp_window_xcb *w, const char *name)
{
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;
	xcb_atom_t atom;

	cookie = xcb_intern_atom(w->connection, 0, strlen(name), name);
	reply = xcb_intern_atom_reply(w->connection, cookie, NULL);
	if (reply) {
		atom = reply->atom;
	} else {
		atom = XCB_NONE;
	}

	free(reply);
	return atom;
}

static VkResult
comp_window_xcb_create_surface(struct comp_window_xcb *w, VkSurfaceKHR *surface)
{
	struct vk_bundle *vk = get_vk(w);
	VkResult ret;

	VkXcbSurfaceCreateInfoKHR surface_info = {
	    .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
	    .connection = w->connection,
	    .window = w->window,
	};

	ret = vk->vkCreateXcbSurfaceKHR(vk->instance, &surface_info, NULL, surface);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.base.c, "vkCreateXcbSurfaceKHR: %s", vk_result_string(ret));
		return ret;
	}

	return VK_SUCCESS;
}

static void
comp_window_xcb_update_window_title(struct comp_target *ct, const char *title)
{
	struct comp_window_xcb *w_xcb = (struct comp_window_xcb *)ct;

	xcb_change_property(w_xcb->connection, XCB_PROP_MODE_REPLACE, w_xcb->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
	                    8, strlen(title), title);
}
