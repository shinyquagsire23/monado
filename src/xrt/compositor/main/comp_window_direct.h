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
comp_window_direct_get_primary_display_mode(struct comp_window *w,
                                            VkDisplayKHR display);

VkResult
comp_window_direct_create_surface(struct comp_window *w,
                                  VkDisplayKHR display,
                                  uint32_t width,
                                  uint32_t height);

int
comp_window_direct_connect(struct comp_window *w, Display **dpy);

VkResult
comp_window_direct_acquire_xlib_display(struct comp_window *w,
                                        Display *dpy,
                                        VkDisplayKHR display);

#ifdef __cplusplus
}
#endif
