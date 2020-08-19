// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Android window code.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include <errno.h>
#include <linux/input.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include "xrt/xrt_compiler.h"
#include "main/comp_window.h"
#include "util/u_misc.h"
#include "android/android_globals.h"
#include "android/android_custom_surface.h"

#include <android/native_window.h>


/*
 *
 * Private structs.
 *
 */

/*!
 * An Android window.
 *
 * @implements comp_window
 */
struct comp_window_android
{
	struct comp_window base;
	struct android_custom_surface *custom_surface;
};

/*
 *
 * Functions.
 *
 */

static bool
comp_window_android_init(struct comp_window *w)
{
	struct comp_window_android *w_android = (struct comp_window_android *)w;
	return true;
}
static void
comp_window_android_destroy(struct comp_window *w)
{
	struct comp_window_android *w_android = (struct comp_window_android *)w;
	android_custom_surface_destroy(&w_android->custom_surface);

	free(w);
}

static void
comp_window_android_update_window_title(struct comp_window *w,
                                        const char *title)
{
	struct comp_window_android *w_android = (struct comp_window_android *)w;
}

static VkResult
comp_window_android_create_surface(struct comp_window_android *w,
                                   VkSurfaceKHR *vk_surface)
{
	struct vk_bundle *vk = w->base.swapchain.vk;
	VkResult ret;
	w->custom_surface = android_custom_surface_async_start(
	    android_globals_get_vm(), android_globals_get_activity());
	if (w->custom_surface == NULL) {
		COMP_ERROR(
		    w->base.c,
		    "comp_window_android_create_surface: could not "
		    "start asynchronous attachment of our custom surface");
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	struct ANativeWindow *window =
	    android_custom_surface_wait_get_surface(w->custom_surface, 2000);
	if (window == NULL) {
		COMP_ERROR(w->base.c,
		           "comp_window_android_create_surface: could not "
		           "convert surface to ANativeWindow");
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	VkAndroidSurfaceCreateInfoKHR surface_info = {
	    .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
	    .flags = 0,
	    .window = window,
	};

	ret = vk->vkCreateAndroidSurfaceKHR(vk->instance, &surface_info, NULL,
	                                    vk_surface);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(w->base.c, "vkCreateAndroidSurfaceKHR: %s",
		           vk_result_string(ret));
		return ret;
	}

	return VK_SUCCESS;
}
static bool
comp_window_android_init_swapchain(struct comp_window *w,
                                   uint32_t width,
                                   uint32_t height)
{
	struct comp_window_android *w_android = (struct comp_window_android *)w;
	VkResult ret;

	ret = comp_window_android_create_surface(w_android,
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


static void
comp_window_android_flush(struct comp_window *w)
{
	struct comp_window_android *w_android = (struct comp_window_android *)w;
}

struct comp_window *
comp_window_android_create(struct comp_compositor *c)
{
	struct comp_window_android *w =
	    U_TYPED_CALLOC(struct comp_window_android);

	w->base.name = "Android";
	w->base.destroy = comp_window_android_destroy;
	w->base.flush = comp_window_android_flush;
	w->base.init = comp_window_android_init;
	w->base.init_swapchain = comp_window_android_init_swapchain;
	w->base.update_window_title = comp_window_android_update_window_title;
	w->base.c = c;

	return &w->base;
}
