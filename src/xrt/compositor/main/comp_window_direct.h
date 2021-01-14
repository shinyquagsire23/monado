// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common direct mode window code header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "main/comp_window.h"

#ifdef __cplusplus
extern "C" {
#endif

VkDisplayModeKHR
comp_window_direct_get_primary_display_mode(struct comp_target_swapchain *cts, VkDisplayKHR display);

VkResult
comp_window_direct_create_surface(struct comp_target_swapchain *cts,
                                  VkDisplayKHR display,
                                  uint32_t width,
                                  uint32_t height);

int
comp_window_direct_connect(struct comp_target_swapchain *cts, Display **dpy);

VkResult
comp_window_direct_acquire_xlib_display(struct comp_target_swapchain *cts, Display *dpy, VkDisplayKHR display);

bool
comp_window_direct_init_swapchain(
    struct comp_target_swapchain *cts, Display *dpy, VkDisplayKHR display, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif
