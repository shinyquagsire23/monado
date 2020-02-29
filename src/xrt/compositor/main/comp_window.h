// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor window header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp
 */

#pragma once

#include "main/comp_vk_swapchain.h"
#include "main/comp_compositor.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Structs
 *
 */

/*!
 * A output device or a window, often directly connected to the device.
 *
 * @ingroup comp
 */
struct comp_window
{
	//! Owning compositor.
	struct comp_compositor *c;

	//! Name of the window system.
	const char *name;

	//! Helper struct.
	struct vk_swapchain swapchain;

	void (*destroy)(struct comp_window *w);
	void (*flush)(struct comp_window *w);
	bool (*init)(struct comp_window *w);
	bool (*init_swapchain)(struct comp_window *w,
	                       uint32_t width,
	                       uint32_t height);
	void (*update_window_title)(struct comp_window *w, const char *title);
};


/*
 *
 * Functions.
 *
 */

#ifdef VK_USE_PLATFORM_XCB_KHR
/*!
 * Create a xcb window.
 *
 * @ingroup comp
 */
struct comp_window *
comp_window_xcb_create(struct comp_compositor *c);
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
/*!
 * Create a wayland window.
 *
 * @ingroup comp
 */
struct comp_window *
comp_window_wayland_create(struct comp_compositor *c);
#endif

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
/*!
 * Create a direct surface to a HMD.
 *
 * @ingroup comp
 */
struct comp_window *
comp_window_direct_create(struct comp_compositor *c);
#endif


#ifdef __cplusplus
}
#endif
