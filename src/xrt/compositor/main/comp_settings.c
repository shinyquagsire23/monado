// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Settings struct for compositor.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp
 */

#include "util/u_debug.h"
#include "comp_settings.h"

DEBUG_GET_ONCE_BOOL_OPTION(print_spew, "XRT_COMPOSITOR_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(print_debug, "XRT_COMPOSITOR_PRINT_DEBUG", false)
DEBUG_GET_ONCE_BOOL_OPTION(force_direct, "XRT_COMPOSITOR_FORCE_DIRECT", false)
DEBUG_GET_ONCE_BOOL_OPTION(force_xcb, "XRT_COMPOSITOR_FORCE_XCB", false)
DEBUG_GET_ONCE_BOOL_OPTION(force_wayland, "XRT_COMPOSITOR_FORCE_WAYLAND", false)

void
comp_settings_init(struct comp_settings *s, struct xrt_device *xdev)
{
	s->display = -1;
	s->mode = -1;
	s->color_format = VK_FORMAT_B8G8R8A8_UNORM;
	s->color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	s->present_mode = VK_PRESENT_MODE_FIFO_KHR;
	s->window_type = WINDOW_AUTO;
	s->fullscreen = false;
	s->flip_y = false;
	s->distortion_model = xdev->distortion.preferred;
	s->width = xdev->screens[0].w_pixels;
	s->height = xdev->screens[0].h_pixels;
	s->print_spew = debug_get_bool_option_print_spew();
	s->print_debug = debug_get_bool_option_print_debug();

	if (debug_get_bool_option_force_direct()) {
		s->window_type = WINDOW_DIRECT_MODE;
	}
	if (debug_get_bool_option_force_xcb()) {
		s->window_type = WINDOW_XCB;
		// HMD screen tends to be much larger then monitors.
		s->width /= 2;
		s->height /= 2;
	}
	if (debug_get_bool_option_force_wayland()) {
		s->window_type = WINDOW_WAYLAND;
		// HMD screen tends to be much larger then monitors.
		s->width /= 2;
		s->height /= 2;
	}
}
