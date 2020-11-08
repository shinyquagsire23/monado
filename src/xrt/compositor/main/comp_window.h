// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor window header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "main/comp_target_swapchain.h"
#include "main/comp_compositor.h"

#include "xrt/xrt_config_os.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Structs
 *
 */

/*!
 * @interface comp_window
 * A output device or a window, often directly connected to the device.
 *
 * @ingroup comp_main
 */
struct comp_window
{
	//! This has to be first.
	struct comp_target_swapchain swapchain;

	//! Owning compositor.
	struct comp_compositor *c;
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
 * @ingroup comp_main
 * @public @memberof comp_window_xcb
 */
struct comp_window *
comp_window_xcb_create(struct comp_compositor *c);
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
/*!
 * Create a wayland window.
 *
 * @ingroup comp_main
 * @public @memberof comp_window_wayland
 */
struct comp_window *
comp_window_wayland_create(struct comp_compositor *c);
#endif

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
/*!
 * Create a direct surface to an HMD over RandR.
 *
 * @ingroup comp_main
 * @public @memberof comp_window_direct_randr
 */
struct comp_window *
comp_window_direct_randr_create(struct comp_compositor *c);

/*!
 * Create a direct surface to an HMD on NVIDIA.
 *
 * @ingroup comp_main
 * @public @memberof comp_window_direct_nvidia
 */
struct comp_window *
comp_window_direct_nvidia_create(struct comp_compositor *c);
#endif


#ifdef XRT_OS_ANDROID

/*!
 * Create a surface to an HMD on Android.
 *
 * @ingroup comp_main
 * @public @memberof comp_window_android
 */
struct comp_window *
comp_window_android_create(struct comp_compositor *c);

#endif // XRT_OS_ANDROID

#ifdef __cplusplus
}
#endif
