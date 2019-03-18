// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wayland window code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp
 */

#ifdef VK_USE_PLATFORM_WAYLAND_KHR

#include <poll.h>
#include <linux/input.h>
#include <wayland-client.h>
#include "xdg-shell-unstable-v6.h"

#include <map>
#include <vector>
#include <string>
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
 * Wayland display mode.
 */
struct comp_window_wayland_mode
{
	std::pair<int, int> size;
	int refresh;
};

/*!
 * A single Wayland display.
 */
struct comp_window_wayland_display
{
	wl_output *output;
	std::string make;
	std::string model;
	std::vector<comp_window_wayland_mode> modes;
	std::pair<int, int> physical_size_mm;
	std::pair<int, int> position;
};

/*!
 * A Wayland connection and window.
 */
struct comp_window_wayland
{
	struct comp_window base = comp_window();

	wl_display *display = nullptr;
	wl_compositor *compositor = nullptr;
	wl_surface *surface = nullptr;

	zxdg_shell_v6 *shell = nullptr;
	zxdg_surface_v6 *xdg_surface = nullptr;
	zxdg_toplevel_v6 *xdg_toplevel = nullptr;

	std::vector<comp_window_wayland_display> displays = {};

	bool is_configured = false;
	bool first_configure = true;
	bool fullscreen_requested = false;
};


/*
 *
 * Pre declare functions.
 *
 */

static void
comp_window_wayland_destroy(struct comp_window *w);

static bool
comp_window_wayland_init(struct comp_window *w);

static void
comp_window_wayland_update_window_title(struct comp_window *w,
                                        const char *title);

static void
comp_window_wayland_registry_global(struct comp_window_wayland *w,
                                    wl_registry *registry,
                                    uint32_t name,
                                    const char *interface);

static void
comp_window_wayland_fullscreen(struct comp_window_wayland *w);

static void
comp_window_wayland_fullscreen(struct comp_window_wayland *w,
                               wl_output *output);

static bool
comp_window_wayland_init_swapchain(struct comp_window *w,
                                   uint32_t width,
                                   uint32_t height);

static VkResult
comp_window_wayland_create_surface(struct comp_window_wayland *w,
                                   VkSurfaceKHR *vk_surface);

static void
comp_window_wayland_flush(struct comp_window *w);

static void
comp_window_wayland_output_mode(struct comp_window_wayland *w,
                                wl_output *output,
                                unsigned int flags,
                                int width,
                                int height,
                                int refresh);

static comp_window_wayland_display *
comp_window_wayland_get_display_from_output(struct comp_window_wayland *w,
                                            wl_output *output);

XRT_MAYBE_UNUSED static void
comp_window_wayland_print_displays(struct comp_window_wayland *w);

static comp_window_wayland_display *
comp_window_wayland_current_display(struct comp_window_wayland *w);

static comp_window_wayland_mode *
comp_window_wayland_current_mode(struct comp_window_wayland *w);

static std::string
mode_to_string(comp_window_wayland_mode *m);

static void
comp_window_wayland_validate_display(struct comp_window_wayland *w);

static void
validate_mode(struct comp_window_wayland *w);

static void
comp_window_wayland_configure(struct comp_window_wayland *w,
                              int32_t width,
                              int32_t height);


/*
 *
 * Functions.
 *
 */

extern "C" struct comp_window *
comp_window_wayland_create(struct comp_compositor *c)
{
	auto w = new comp_window_wayland();

	w->base.name = "wayland";
	w->base.destroy = comp_window_wayland_destroy;
	w->base.flush = comp_window_wayland_flush;
	w->base.init = comp_window_wayland_init;
	w->base.init_swapchain = comp_window_wayland_init_swapchain;
	w->base.update_window_title = comp_window_wayland_update_window_title;
	w->base.c = c;

	return &w->base;
}

static void
comp_window_wayland_destroy(struct comp_window *w)
{
	struct comp_window_wayland *w_wayland = (struct comp_window_wayland *)w;

	if (w_wayland->surface) {
		wl_surface_destroy(w_wayland->surface);
		w_wayland->surface = nullptr;
	}
	if (w_wayland->compositor) {
		wl_compositor_destroy(w_wayland->compositor);
		w_wayland->compositor = nullptr;
	}
	if (w_wayland->display) {
		wl_display_disconnect(w_wayland->display);
		w_wayland->display = nullptr;
	}

	delete w;
}

static void
comp_window_wayland_update_window_title(struct comp_window *w,
                                        const char *title)
{
	struct comp_window_wayland *w_wayland = (struct comp_window_wayland *)w;
	zxdg_toplevel_v6_set_title(w_wayland->xdg_toplevel, title);
}

static void
comp_window_wayland_fullscreen(struct comp_window_wayland *w)
{
	comp_window_wayland_fullscreen(
	    w, comp_window_wayland_current_display(w)->output);
}

static void
comp_window_wayland_fullscreen(struct comp_window_wayland *w, wl_output *output)
{
	zxdg_toplevel_v6_set_fullscreen(w->xdg_toplevel, output);
	wl_surface_commit(w->surface);
}

static void
_xdg_surface_configure_cb(void *data, zxdg_surface_v6 *surface, uint32_t serial)
{
	zxdg_surface_v6_ack_configure(surface, serial);
}

static void
_xdg_toplevel_configure_cb(void *data,
                           zxdg_toplevel_v6 *toplevel,
                           int32_t width,
                           int32_t height,
                           struct wl_array *states)
{
	comp_window_wayland *w = (comp_window_wayland *)data;
	comp_window_wayland_configure(w, width, height);
}

static const zxdg_surface_v6_listener xdg_surface_listener = {
    _xdg_surface_configure_cb,
};

static void
_xdg_toplevel_close_cb(void *data, zxdg_toplevel_v6 *toplevel)
{}

static const zxdg_toplevel_v6_listener xdg_toplevel_listener = {
    _xdg_toplevel_configure_cb,
    _xdg_toplevel_close_cb,
};

static void
_xdg_shell_ping_cb(void *data, zxdg_shell_v6 *shell, uint32_t serial)
{
	zxdg_shell_v6_pong(shell, serial);
}

const zxdg_shell_v6_listener xdg_shell_listener = {
    _xdg_shell_ping_cb,
};

static bool
comp_window_wayland_init_swapchain(struct comp_window *w,
                                   uint32_t width,
                                   uint32_t height)
{
	struct comp_window_wayland *w_wayland = (struct comp_window_wayland *)w;
	VkResult ret;

	ret = comp_window_wayland_create_surface(w_wayland,
	                                         &w->swapchain.surface);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->c, "Failed to create surface!");
		return false;
	}

	vk_swapchain_create(
	    &w->swapchain, width, height, w->c->settings.color_format,
	    w->c->settings.color_space, w->c->settings.present_mode);

	return true;
}

static VkResult
comp_window_wayland_create_surface(struct comp_window_wayland *w,
                                   VkSurfaceKHR *vk_surface)
{
	struct vk_bundle *vk = w->base.swapchain.vk;
	VkResult ret;

	VkWaylandSurfaceCreateInfoKHR surface_info = {
	    .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
	    .pNext = nullptr,
	    .flags = 0,
	    .display = w->display,
	    .surface = w->surface,
	};

	ret = vk->vkCreateWaylandSurfaceKHR(vk->instance, &surface_info, NULL,
	                                    vk_surface);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c, "vkCreateWaylandSurfaceKHR: %s",
		           vk_result_string(ret));
		return ret;
	}

	return VK_SUCCESS;
}

static void
comp_window_wayland_flush(struct comp_window *w)
{
	struct comp_window_wayland *w_wayland = (struct comp_window_wayland *)w;

	while (wl_display_prepare_read(w_wayland->display) != 0)
		wl_display_dispatch_pending(w_wayland->display);
	if (wl_display_flush(w_wayland->display) < 0 && errno != EAGAIN) {
		wl_display_cancel_read(w_wayland->display);
		return;
	}

	struct pollfd fds[] = {
	    {
	        .fd = wl_display_get_fd(w_wayland->display),
	        .events = POLLIN,
	        .revents = 0,
	    },
	};

	if (poll(fds, 1, 0) > 0) {
		wl_display_read_events(w_wayland->display);
		wl_display_dispatch_pending(w_wayland->display);
	} else {
		wl_display_cancel_read(w_wayland->display);
	}
}

static void
comp_window_wayland_output_mode(struct comp_window_wayland *w,
                                wl_output *output,
                                unsigned int flags,
                                int width,
                                int height,
                                int refresh)
{
	comp_window_wayland_mode m = {};
	m.size = {width, height};
	m.refresh = refresh;

	comp_window_wayland_display *d =
	    comp_window_wayland_get_display_from_output(w, output);
	if (d == nullptr) {
		COMP_ERROR(w->base.c, "Output mode callback before geomentry!");
		return;
	}

	d->modes.push_back(m);
}

static void
_output_done_cb(void *data, wl_output *output)
{}

static void
_output_scale_cb(void *data, wl_output *output, int scale)
{}

static void
_registry_global_remove_cb(void *data, wl_registry *registry, uint32_t name)
{}

static void
_registry_global_cb(void *data,
                    wl_registry *registry,
                    uint32_t name,
                    const char *interface,
                    uint32_t version)
{
	comp_window_wayland *w = (comp_window_wayland *)data;
	// vik_log_d("Interface: %s Version %d", interface, version);
	comp_window_wayland_registry_global(w, registry, name, interface);
}

static void
_output_mode_cb(void *data,
                wl_output *output,
                unsigned int flags,
                int width,
                int height,
                int refresh)
{
	comp_window_wayland *w = (comp_window_wayland *)data;
	comp_window_wayland_output_mode(w, output, flags, width, height,
	                                refresh);
}

static void
_output_geometry_cb(void *data,
                    wl_output *output,
                    int x,
                    int y,
                    int w,
                    int h,
                    int subpixel,
                    const char *make,
                    const char *model,
                    int transform)
{
	comp_window_wayland_display d = {};
	d.output = output;
	d.make = std::string(make);
	d.model = std::string(model);
	d.physical_size_mm = {w, h};
	d.position = {x, y};

	comp_window_wayland *self = (comp_window_wayland *)data;
	self->displays.push_back(d);
}

// listeners
static const wl_registry_listener registry_listener = {
    _registry_global_cb,
    _registry_global_remove_cb,
};

static const wl_output_listener output_listener = {
    _output_geometry_cb,
    _output_mode_cb,
    _output_done_cb,
    _output_scale_cb,
};

static void
comp_window_wayland_registry_global(struct comp_window_wayland *w,
                                    wl_registry *registry,
                                    uint32_t name,
                                    const char *interface)
{
	if (strcmp(interface, "wl_compositor") == 0) {
		w->compositor = (wl_compositor *)wl_registry_bind(
		    registry, name, &wl_compositor_interface, 4);
	} else if (strcmp(interface, "zxdg_shell_v6") == 0) {
		w->shell = (zxdg_shell_v6 *)wl_registry_bind(
		    registry, name, &zxdg_shell_v6_interface, 1);
		zxdg_shell_v6_add_listener(w->shell, &xdg_shell_listener, w);
	} else if (strcmp(interface, "wl_output") == 0) {
		wl_output *_output = (wl_output *)wl_registry_bind(
		    registry, name, &wl_output_interface, 2);
		wl_output_add_listener(_output, &output_listener, w);
	}
}

static bool
comp_window_wayland_init(struct comp_window *w)
{
	struct comp_window_wayland *w_wayland = (struct comp_window_wayland *)w;

	w_wayland->display = wl_display_connect(NULL);
	if (!w_wayland->display) {
		return false;
	}

	wl_registry *registry = wl_display_get_registry(w_wayland->display);
	wl_registry_add_listener(registry, &registry_listener, w_wayland);

	wl_display_roundtrip(w_wayland->display);

	wl_registry_destroy(registry);

	w_wayland->surface =
	    wl_compositor_create_surface(w_wayland->compositor);

	if (!w_wayland->shell) {
		COMP_ERROR(
		    w->c,
		    "Compositor is missing unstable zxdg_shell_v6 support");
	}

	w_wayland->xdg_surface =
	    zxdg_shell_v6_get_xdg_surface(w_wayland->shell, w_wayland->surface);

	zxdg_surface_v6_add_listener(w_wayland->xdg_surface,
	                             &xdg_surface_listener, w_wayland);

	w_wayland->xdg_toplevel =
	    zxdg_surface_v6_get_toplevel(w_wayland->xdg_surface);

	zxdg_toplevel_v6_add_listener(w_wayland->xdg_toplevel,
	                              &xdg_toplevel_listener, w_wayland);

	wl_surface_commit(w_wayland->surface);

	return true;
}

static comp_window_wayland_display *
comp_window_wayland_get_display_from_output(struct comp_window_wayland *w,
                                            wl_output *output)
{
	for (int i = 0; i < (int)w->displays.size(); i++) {
		if (w->displays[i].output == output)
			return &w->displays[i];
	}
	return nullptr;
}

XRT_MAYBE_UNUSED static void
comp_window_wayland_print_displays(struct comp_window_wayland *w)
{
	int i_d = 0;
	COMP_DEBUG(w->base.c, "Available displays:");
	for (auto d : w->displays) {
		COMP_DEBUG(w->base.c, "%d: %s %s [%d, %d] %dx%dmm (%d Modes)",
		           i_d, d.make.c_str(), d.model.c_str(),
		           d.position.first, d.position.second,
		           d.physical_size_mm.first, d.physical_size_mm.second,
		           (int)d.modes.size());

		int i_m = 0;
		for (auto m : d.modes) {
			COMP_DEBUG(w->base.c, "\t%d: %s", i_m,
			           mode_to_string(&m).c_str());
			i_m++;
		}
		i_d++;
	}
}

static comp_window_wayland_display *
comp_window_wayland_current_display(struct comp_window_wayland *w)
{
	return &w->displays[w->base.c->settings.display];
}

static comp_window_wayland_mode *
comp_window_wayland_current_mode(struct comp_window_wayland *w)
{
	return &comp_window_wayland_current_display(w)
	            ->modes[w->base.c->settings.mode];
}

static std::string
mode_to_string(comp_window_wayland_mode *m)
{
	auto size = std::snprintf(nullptr, 0, "%d x %d @ %.2fHz", m->size.first,
	                          m->size.second, (float)m->refresh / 1000.0);
	std::string output(size + 1, '\0');
	std::snprintf(&output[0], size, "%d x %d @ %.2fHz", m->size.first,
	              m->size.second, (float)m->refresh / 1000.0);
	return std::string(output);
}

static void
comp_window_wayland_validate_display(struct comp_window_wayland *w)
{
	comp_window_wayland_display *d;

	if (w->base.c->settings.display < 0)
		w->base.c->settings.display = 0;

	if (w->base.c->settings.display > (int)w->displays.size()) {
		COMP_DEBUG(w->base.c,
		           "Requested display %d, but only %d displays are "
		           "available.",
		           w->base.c->settings.display,
		           (int)w->displays.size());

		w->base.c->settings.display = 0;
		d = comp_window_wayland_current_display(w);
		COMP_DEBUG(w->base.c, "Selecting '%s %s' instead.",
		           d->make.c_str(), d->model.c_str());
	}
}

static void
validate_mode(struct comp_window_wayland *w)
{
	comp_window_wayland_display *d = comp_window_wayland_current_display(w);

	if (w->base.c->settings.mode < 0)
		w->base.c->settings.mode = 0;

	if (w->base.c->settings.mode > (int)d->modes.size()) {
		COMP_DEBUG(w->base.c,
		           "Requested mode %d, but only %d modes"
		           " are available on display %d.",
		           w->base.c->settings.mode, (int)d->modes.size(),
		           w->base.c->settings.display);
		w->base.c->settings.mode = 0;
		COMP_DEBUG(w->base.c, "Selecting '%s' instead",
		           mode_to_string(comp_window_wayland_current_mode(w))
		               .c_str());
	}
}

static void
comp_window_wayland_configure(struct comp_window_wayland *w,
                              int32_t width,
                              int32_t height)
{
	if (w->first_configure) {
		comp_window_wayland_validate_display(w);
		validate_mode(w);
		w->first_configure = false;
	}

	comp_window_wayland_mode *m = comp_window_wayland_current_mode(w);
	if (w->fullscreen_requested &&
	    (m->size.first != width || m->size.second != height)) {
		COMP_DEBUG(w->base.c,
		           "Received mode %dx%d does not match requested Mode "
		           "%dx%d. "
		           "Compositor bug? Requesting again.",
		           width, height, m->size.first, m->size.second);
		w->fullscreen_requested = false;
	}

	m = comp_window_wayland_current_mode(w);
	if (w->base.c->settings.fullscreen && !w->fullscreen_requested) {
		COMP_DEBUG(
		    w->base.c, "Setting full screen on Display %d Mode %s",
		    w->base.c->settings.display, mode_to_string(m).c_str());
		comp_window_wayland_fullscreen(w);
		w->fullscreen_requested = true;
		// TODO: resize cb
		// resize_cb(m->size.first, m->size.second);
	}
}

#endif
