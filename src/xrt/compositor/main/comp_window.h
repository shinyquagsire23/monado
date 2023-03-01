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
struct comp_target *
comp_window_xcb_create(struct comp_compositor *c);

extern const struct comp_target_factory comp_target_factory_xcb;

#endif // VK_USE_PLATFORM_XCB_KHR

#ifdef VK_USE_PLATFORM_WAYLAND_KHR

/*!
 * Create a wayland window.
 *
 * @ingroup comp_main
 * @public @memberof comp_window_wayland
 */
struct comp_target *
comp_window_wayland_create(struct comp_compositor *c);

extern const struct comp_target_factory comp_target_factory_wayland;

/*!
 * Create a direct surface to a HMD using Wayland.
 *
 * @ingroup comp
 */
struct comp_target *
comp_window_direct_wayland_create(struct comp_compositor *c);

extern const struct comp_target_factory comp_target_factory_direct_wayland;

#endif // VK_USE_PLATFORM_WAYLAND_KHR

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
/*!
 * Create a direct surface to an HMD over RandR.
 *
 * @ingroup comp_main
 * @public @memberof comp_window_direct_randr
 */
struct comp_target *
comp_window_direct_randr_create(struct comp_compositor *c);

extern const struct comp_target_factory comp_target_factory_direct_randr;

/*!
 * Create a direct surface to an HMD on NVIDIA.
 *
 * @ingroup comp_main
 * @public @memberof comp_window_direct_nvidia
 */
struct comp_target *
comp_window_direct_nvidia_create(struct comp_compositor *c);

extern const struct comp_target_factory comp_target_factory_direct_nvidia;
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT

#if 1
/*!
 * Create a direct surface to an HMD on VkDisplay.
 *
 * @ingroup comp_main
 * @public @memberof comp_window_direct_vk_display
 */
struct comp_target *
comp_window_vk_display_create(struct comp_compositor *c);

extern const struct comp_target_factory comp_target_factory_vk_display;
#endif // 1

#ifdef XRT_OS_ANDROID
/*!
 * Create a surface to an HMD on Android.
 *
 * @ingroup comp_main
 * @public @memberof comp_window_android
 */
struct comp_target *
comp_window_android_create(struct comp_compositor *c);

extern const struct comp_target_factory comp_target_factory_android;
#endif // XRT_OS_ANDROID

#ifdef XRT_OS_WINDOWS

/*!
 * Create a rendering window on Windows.
 *
 * @ingroup comp_main
 * @public @memberof comp_window_mswin
 */
struct comp_target *
comp_window_mswin_create(struct comp_compositor *c);

extern const struct comp_target_factory comp_target_factory_mswin;
#endif // XRT_OS_WINDOWS

#ifdef __cplusplus
}
#endif
