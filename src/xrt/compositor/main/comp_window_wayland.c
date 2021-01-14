// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wayland window code.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include <errno.h>
#include <linux/input.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#include "xrt/xrt_compiler.h"
#include "main/comp_window.h"
#include "util/u_misc.h"


/*
 *
 * Private structs.
 *
 */

/*!
 * A Wayland connection and window.
 *
 * @implements comp_target_swapchain
 */
struct comp_window_wayland
{
	struct comp_target_swapchain base;

	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_surface *surface;

	struct xdg_wm_base *wm_base;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

	bool fullscreen_requested;
};


/*
 *
 * Pre declare functions.
 *
 */

static void
comp_window_wayland_destroy(struct comp_target *ct);

static bool
comp_window_wayland_init(struct comp_target *ct);

static void
comp_window_wayland_update_window_title(struct comp_target *ct, const char *title);

static void
comp_window_wayland_registry_global(struct comp_window_wayland *w,
                                    struct wl_registry *registry,
                                    uint32_t name,
                                    const char *interface);

static void
comp_window_wayland_fullscreen(struct comp_window_wayland *w);

static bool
comp_window_wayland_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height);

static VkResult
comp_window_wayland_create_surface(struct comp_window_wayland *w, VkSurfaceKHR *vk_surface);

static void
comp_window_wayland_flush(struct comp_target *ct);

static void
comp_window_wayland_configure(struct comp_window_wayland *w, int32_t width, int32_t height);


/*
 *
 * Functions.
 *
 */

static inline struct vk_bundle *
get_vk(struct comp_window_wayland *cww)
{
	return &cww->base.base.c->vk;
}

struct comp_target *
comp_window_wayland_create(struct comp_compositor *c)
{
	struct comp_window_wayland *w = U_TYPED_CALLOC(struct comp_window_wayland);

	comp_target_swapchain_init_set_fnptrs(&w->base);

	w->base.base.name = "wayland";
	w->base.base.destroy = comp_window_wayland_destroy;
	w->base.base.flush = comp_window_wayland_flush;
	w->base.base.init_pre_vulkan = comp_window_wayland_init;
	w->base.base.init_post_vulkan = comp_window_wayland_init_swapchain;
	w->base.base.set_title = comp_window_wayland_update_window_title;
	w->base.base.c = c;

	return &w->base.base;
}

static void
comp_window_wayland_destroy(struct comp_target *ct)
{
	struct comp_window_wayland *cww = (struct comp_window_wayland *)ct;

	comp_target_swapchain_cleanup(&cww->base);

	if (cww->surface) {
		wl_surface_destroy(cww->surface);
		cww->surface = NULL;
	}
	if (cww->compositor) {
		wl_compositor_destroy(cww->compositor);
		cww->compositor = NULL;
	}
	if (cww->display) {
		wl_display_disconnect(cww->display);
		cww->display = NULL;
	}

	free(ct);
}

static void
comp_window_wayland_update_window_title(struct comp_target *ct, const char *title)
{
	struct comp_window_wayland *w_wayland = (struct comp_window_wayland *)ct;
	xdg_toplevel_set_title(w_wayland->xdg_toplevel, title);
}

static void
comp_window_wayland_fullscreen(struct comp_window_wayland *w)
{
	xdg_toplevel_set_fullscreen(w->xdg_toplevel, NULL);
	wl_surface_commit(w->surface);
}

static void
_xdg_surface_configure_cb(void *data, struct xdg_surface *surface, uint32_t serial)
{
	xdg_surface_ack_configure(surface, serial);
}

static void
_xdg_toplevel_configure_cb(
    void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states)
{
	struct comp_window_wayland *w = (struct comp_window_wayland *)data;
	comp_window_wayland_configure(w, width, height);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    _xdg_surface_configure_cb,
};

static void
_xdg_toplevel_close_cb(void *data, struct xdg_toplevel *toplevel)
{}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    _xdg_toplevel_configure_cb,
    _xdg_toplevel_close_cb,
};

static void
_xdg_wm_base_ping_cb(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    _xdg_wm_base_ping_cb,
};

static bool
comp_window_wayland_init_swapchain(struct comp_target *ct, uint32_t width, uint32_t height)
{
	struct comp_window_wayland *w_wayland = (struct comp_window_wayland *)ct;
	VkResult ret;

	ret = comp_window_wayland_create_surface(w_wayland, &w_wayland->base.surface.handle);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(ct->c, "Failed to create surface!");
		return false;
	}

	xdg_toplevel_set_min_size(w_wayland->xdg_toplevel, width, height);
	xdg_toplevel_set_max_size(w_wayland->xdg_toplevel, width, height);

	return true;
}

static VkResult
comp_window_wayland_create_surface(struct comp_window_wayland *w, VkSurfaceKHR *vk_surface)
{
	struct vk_bundle *vk = get_vk(w);
	VkResult ret;

	VkWaylandSurfaceCreateInfoKHR surface_info = {
	    .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
	    .display = w->display,
	    .surface = w->surface,
	};

	ret = vk->vkCreateWaylandSurfaceKHR(vk->instance, &surface_info, NULL, vk_surface);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.base.c, "vkCreateWaylandSurfaceKHR: %s", vk_result_string(ret));
		return ret;
	}

	return VK_SUCCESS;
}

static void
comp_window_wayland_flush(struct comp_target *ct)
{
	struct comp_window_wayland *w_wayland = (struct comp_window_wayland *)ct;

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
_registry_global_remove_cb(void *data, struct wl_registry *registry, uint32_t name)
{}

static void
_registry_global_cb(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
	struct comp_window_wayland *w = (struct comp_window_wayland *)data;
	// vik_log_d("Interface: %s Version %d", interface, version);
	comp_window_wayland_registry_global(w, registry, name, interface);
}

static const struct wl_registry_listener registry_listener = {
    _registry_global_cb,
    _registry_global_remove_cb,
};

static void
comp_window_wayland_registry_global(struct comp_window_wayland *w,
                                    struct wl_registry *registry,
                                    uint32_t name,
                                    const char *interface)
{
	if (strcmp(interface, "wl_compositor") == 0) {
		w->compositor = (struct wl_compositor *)wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		w->wm_base = (struct xdg_wm_base *)wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(w->wm_base, &xdg_wm_base_listener, w);
	}
}

static bool
comp_window_wayland_init(struct comp_target *ct)
{
	struct comp_window_wayland *w_wayland = (struct comp_window_wayland *)ct;

	w_wayland->display = wl_display_connect(NULL);
	if (!w_wayland->display) {
		return false;
	}

	struct wl_registry *registry = wl_display_get_registry(w_wayland->display);
	wl_registry_add_listener(registry, &registry_listener, w_wayland);

	wl_display_roundtrip(w_wayland->display);

	wl_registry_destroy(registry);

	w_wayland->surface = wl_compositor_create_surface(w_wayland->compositor);

	if (!w_wayland->wm_base) {
		COMP_ERROR(ct->c, "Compositor is missing xdg-shell support");
	}

	w_wayland->xdg_surface = xdg_wm_base_get_xdg_surface(w_wayland->wm_base, w_wayland->surface);

	xdg_surface_add_listener(w_wayland->xdg_surface, &xdg_surface_listener, w_wayland);

	w_wayland->xdg_toplevel = xdg_surface_get_toplevel(w_wayland->xdg_surface);

	xdg_toplevel_add_listener(w_wayland->xdg_toplevel, &xdg_toplevel_listener, w_wayland);
	/* Sane defaults */
	xdg_toplevel_set_app_id(w_wayland->xdg_toplevel, "openxr");
	xdg_toplevel_set_title(w_wayland->xdg_toplevel, "OpenXR application");

	wl_surface_commit(w_wayland->surface);

	return true;
}

static void
comp_window_wayland_configure(struct comp_window_wayland *w, int32_t width, int32_t height)
{
	if (w->base.base.c->settings.fullscreen && !w->fullscreen_requested) {
		COMP_DEBUG(w->base.base.c, "Setting full screen");
		comp_window_wayland_fullscreen(w);
		w->fullscreen_requested = true;
	}
}
