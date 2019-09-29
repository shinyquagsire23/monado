// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  XCB window code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp
 */

#ifdef VK_USE_PLATFORM_XCB_KHR

#include <xcb/xcb.h>
#include <xcb/randr.h>

#include <string>
#include <vector>
#include <utility>
#include <cstring>

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
	std::string name;
	std::pair<int16_t, int16_t> position;
	std::pair<uint16_t, uint16_t> size;
};

/*!
 * A xcb connection and window.
 */
struct comp_window_xcb
{
	struct comp_window base = comp_window();

	xcb_connection_t *connection = nullptr;
	xcb_window_t window = XCB_NONE;
	xcb_screen_t *screen = nullptr;

	xcb_atom_t atom_wm_protocols = XCB_NONE;
	xcb_atom_t atom_wm_delete_window = XCB_NONE;

	std::vector<comp_window_xcb_display> displays = {};
};


/*
 *
 * Pre declare functions.
 *
 */

static void
comp_window_xcb_destroy(struct comp_window *w);

static void
comp_window_xcb_flush(struct comp_window *w);

XRT_MAYBE_UNUSED static void
comp_window_xcb_list_screens(comp_window_xcb *w, xcb_screen_t *screen);

static bool
comp_window_xcb_init(struct comp_window *w);

static comp_window_xcb_display *
comp_window_xcb_current_display(comp_window_xcb *w);

static bool
comp_window_xcb_init_swapchain(struct comp_window *w,
                               uint32_t width,
                               uint32_t height);

static int
comp_window_xcb_connect(comp_window_xcb *w);

static void
comp_window_xcb_create_window(comp_window_xcb *w,
                              uint32_t width,
                              uint32_t height);

static void
comp_window_xcb_get_randr_outputs(comp_window_xcb *w);

static void
comp_window_xcb_connect_delete_event(comp_window_xcb *w);

static void
comp_window_xcb_set_full_screen(comp_window_xcb *w);

static xcb_atom_t
comp_window_xcb_get_atom(comp_window_xcb *w, const char *name);

static VkResult
comp_window_xcb_create_surface(comp_window_xcb *w, VkSurfaceKHR *surface);

static void
comp_window_xcb_update_window_title(struct comp_window *w, const char *title);


/*
 *
 * Functions.
 *
 */

extern "C" struct comp_window *
comp_window_xcb_create(struct comp_compositor *c)
{
	auto w = new comp_window_xcb();

	w->base.name = "xcb";
	w->base.destroy = comp_window_xcb_destroy;
	w->base.flush = comp_window_xcb_flush;
	w->base.init = comp_window_xcb_init;
	w->base.init_swapchain = comp_window_xcb_init_swapchain;
	w->base.update_window_title = comp_window_xcb_update_window_title;
	w->base.c = c;

	return &w->base;
}

static void
comp_window_xcb_destroy(struct comp_window *w)
{
	comp_window_xcb *w_xcb = (comp_window_xcb *)w;
	xcb_destroy_window(w_xcb->connection, w_xcb->window);
	xcb_disconnect(w_xcb->connection);

	delete w;
}

static void
comp_window_xcb_list_screens(comp_window_xcb *w, xcb_screen_t *screen)
{
	COMP_DEBUG(w->base.c, "Screen 0 %dx%d", screen->width_in_pixels,
	           screen->height_in_pixels);
	comp_window_xcb_get_randr_outputs(w);

	int display_i = 0;
	for (const comp_window_xcb_display &d : w->displays) {
		COMP_DEBUG(w->base.c, "%d: %s %dx%d [%d, %d]", display_i,
		           d.name.c_str(), d.size.first, d.size.second,
		           d.position.first, d.position.second);
		display_i++;
	}
}

static bool
comp_window_xcb_init(struct comp_window *w)
{
	struct comp_window_xcb *w_xcb = (struct comp_window_xcb *)w;

	if (!comp_window_xcb_connect(w_xcb)) {
		return false;
	}

	xcb_screen_iterator_t iter =
	    xcb_setup_roots_iterator(xcb_get_setup(w_xcb->connection));

	w_xcb->screen = iter.data;

	if (w->c->settings.fullscreen) {
		comp_window_xcb_get_randr_outputs(w_xcb);

		if (w->c->settings.display > (int)w_xcb->displays.size() - 1) {
			COMP_DEBUG(w->c,
			           "Requested display %d, but only %d "
			           "displays are available.",
			           w->c->settings.display,
			           (int)w_xcb->displays.size());

			w->c->settings.display = 0;
			comp_window_xcb_display *d =
			    comp_window_xcb_current_display(w_xcb);
			COMP_DEBUG(w->c, "Selecting '%s' instead.",
			           d->name.c_str());
		}

		if (w->c->settings.display == -1)
			w->c->settings.display = 0;

		comp_window_xcb_display *d =
		    comp_window_xcb_current_display(w_xcb);
		w->c->settings.width = d->size.first;
		w->c->settings.height = d->size.second;
		// TODO: size cb
		// set_size_cb(settings->width, settings->height);
	}

	comp_window_xcb_create_window(w_xcb, w->c->settings.width,
	                              w->c->settings.height);

	comp_window_xcb_connect_delete_event(w_xcb);

	if (w->c->settings.fullscreen)
		comp_window_xcb_set_full_screen(w_xcb);

	xcb_map_window(w_xcb->connection, w_xcb->window);

	return true;
}

static comp_window_xcb_display *
comp_window_xcb_current_display(comp_window_xcb *w)
{
	return &w->displays[w->base.c->settings.display];
}

static void
comp_window_xcb_flush(struct comp_window *w)
{}

static bool
comp_window_xcb_init_swapchain(struct comp_window *w,
                               uint32_t width,
                               uint32_t height)
{
	comp_window_xcb *w_xcb = (comp_window_xcb *)w;
	VkResult ret;

	ret = comp_window_xcb_create_surface(w_xcb, &w->swapchain.surface);
	if (ret != VK_SUCCESS) {
		return false;
	}

	// vk_swapchain_set_dimension_cb(&swapchain, set_size_cb);
	// vk_swapchain_set_settings(&w->swapchain, w->settings);
	vk_swapchain_create(
	    &w->swapchain, width, height, w->c->settings.color_format,
	    w->c->settings.color_space, w->c->settings.present_mode);

	return true;
}

static int
comp_window_xcb_connect(comp_window_xcb *w)
{
	w->connection = xcb_connect(nullptr, nullptr);
	return !xcb_connection_has_error(w->connection);
}

static void
comp_window_xcb_create_window(comp_window_xcb *w,
                              uint32_t width,
                              uint32_t height)
{
	w->window = xcb_generate_id(w->connection);

	int x = 0;
	int y = 0;

	if (w->base.c->settings.fullscreen) {
		x = comp_window_xcb_current_display(w)->position.first;
		y = comp_window_xcb_current_display(w)->position.second;
	}

	uint32_t value_list = XCB_EVENT_MASK_STRUCTURE_NOTIFY;

	xcb_create_window(w->connection, XCB_COPY_FROM_PARENT, w->window,
	                  w->screen->root, x, y, width, height, 0,
	                  XCB_WINDOW_CLASS_INPUT_OUTPUT, w->screen->root_visual,
	                  XCB_CW_EVENT_MASK, &value_list);
}

static void
comp_window_xcb_get_randr_outputs(comp_window_xcb *w)
{
	xcb_randr_get_screen_resources_cookie_t resources_cookie =
	    xcb_randr_get_screen_resources(w->connection, w->screen->root);
	xcb_randr_get_screen_resources_reply_t *resources_reply =
	    xcb_randr_get_screen_resources_reply(w->connection,
	                                         resources_cookie, nullptr);
	xcb_randr_output_t *xcb_outputs =
	    xcb_randr_get_screen_resources_outputs(resources_reply);

	int count =
	    xcb_randr_get_screen_resources_outputs_length(resources_reply);
	if (count < 1) {
		COMP_ERROR(w->base.c, "Failed to retrieve randr outputs");
	}

	for (int i = 0; i < count; i++) {
		xcb_randr_get_output_info_cookie_t output_cookie =
		    xcb_randr_get_output_info(w->connection, xcb_outputs[i],
		                              XCB_CURRENT_TIME);
		xcb_randr_get_output_info_reply_t *output_reply =
		    xcb_randr_get_output_info_reply(w->connection,
		                                    output_cookie, nullptr);

		if (output_reply->connection !=
		        XCB_RANDR_CONNECTION_CONNECTED ||
		    output_reply->crtc == XCB_NONE) {
			continue;
		}

		xcb_randr_get_crtc_info_cookie_t crtc_cookie =
		    xcb_randr_get_crtc_info(w->connection, output_reply->crtc,
		                            XCB_CURRENT_TIME);
		xcb_randr_get_crtc_info_reply_t *crtc_reply =
		    xcb_randr_get_crtc_info_reply(w->connection, crtc_cookie,
		                                  nullptr);

		uint8_t *name = xcb_randr_get_output_info_name(output_reply);
		int name_len =
		    xcb_randr_get_output_info_name_length(output_reply);

		char *name_str = (char *)malloc(name_len + 1);
		memcpy(name_str, name, name_len);
		name_str[name_len] = '\0';

		struct comp_window_xcb_display d = {};
		d.name = std::string(name_str);
		d.position = {crtc_reply->x, crtc_reply->y};
		d.size = {crtc_reply->width, crtc_reply->height};
		w->displays.push_back(d);

		free(name_str);
	}
}

static void
comp_window_xcb_connect_delete_event(comp_window_xcb *w)
{
	w->atom_wm_protocols = comp_window_xcb_get_atom(w, "WM_PROTOCOLS");
	w->atom_wm_delete_window =
	    comp_window_xcb_get_atom(w, "WM_DELETE_WINDOW");
	xcb_change_property(w->connection, XCB_PROP_MODE_REPLACE, w->window,
	                    w->atom_wm_protocols, XCB_ATOM_ATOM, 32, 1,
	                    &w->atom_wm_delete_window);
}

static void
comp_window_xcb_set_full_screen(comp_window_xcb *w)
{
	xcb_atom_t atom_wm_state = comp_window_xcb_get_atom(w, "_NET_WM_STATE");
	xcb_atom_t atom_wm_fullscreen =
	    comp_window_xcb_get_atom(w, "_NET_WM_STATE_FULLSCREEN");
	xcb_change_property(w->connection, XCB_PROP_MODE_REPLACE, w->window,
	                    atom_wm_state, XCB_ATOM_ATOM, 32, 1,
	                    &atom_wm_fullscreen);
}

static xcb_atom_t
comp_window_xcb_get_atom(comp_window_xcb *w, const char *name)
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
comp_window_xcb_create_surface(comp_window_xcb *w, VkSurfaceKHR *surface)
{
	struct vk_bundle *vk = w->base.swapchain.vk;
	VkResult ret;

	VkXcbSurfaceCreateInfoKHR surface_info = {
	    .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
	    .pNext = nullptr,
	    .flags = 0,
	    .connection = w->connection,
	    .window = w->window,
	};

	ret = vk->vkCreateXcbSurfaceKHR(vk->instance, &surface_info, NULL,
	                                surface);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c, "vkCreateXcbSurfaceKHR: %s",
		           vk_result_string(ret));
		return ret;
	}

	return VK_SUCCESS;
}

static void
comp_window_xcb_update_window_title(struct comp_window *w, const char *title)
{
	comp_window_xcb *w_xcb = (comp_window_xcb *)w;
	xcb_change_property(w_xcb->connection, XCB_PROP_MODE_REPLACE,
	                    w_xcb->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
	                    strlen(title), title);
}

#endif
