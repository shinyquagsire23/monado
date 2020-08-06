// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Settings struct for compositor.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_main
 */

#include "util/u_debug.h"
#include "comp_settings.h"

// clang-format off
DEBUG_GET_ONCE_BOOL_OPTION(print_spew, "XRT_COMPOSITOR_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(print_debug, "XRT_COMPOSITOR_PRINT_DEBUG", false)
DEBUG_GET_ONCE_BOOL_OPTION(print_modes, "XRT_COMPOSITOR_PRINT_MODES", false)
DEBUG_GET_ONCE_BOOL_OPTION(force_randr, "XRT_COMPOSITOR_FORCE_RANDR", false)
DEBUG_GET_ONCE_BOOL_OPTION(force_nvidia, "XRT_COMPOSITOR_FORCE_NVIDIA", false)
DEBUG_GET_ONCE_BOOL_OPTION(force_xcb, "XRT_COMPOSITOR_FORCE_XCB", false)
DEBUG_GET_ONCE_BOOL_OPTION(force_wayland, "XRT_COMPOSITOR_FORCE_WAYLAND", false)
DEBUG_GET_ONCE_BOOL_OPTION(wireframe, "XRT_COMPOSITOR_WIREFRAME", false)
DEBUG_GET_ONCE_NUM_OPTION(force_gpu_index, "XRT_COMPOSITOR_FORCE_GPU_INDEX", -1)
DEBUG_GET_ONCE_NUM_OPTION(desired_mode, "XRT_COMPOSITOR_DESIRED_MODE", -1)
// clang-format on

void
comp_settings_init(struct comp_settings *s, struct xrt_device *xdev)
{
	uint64_t interval_ns = xdev->hmd->screens[0].nominal_frame_interval_ns;
	if (interval_ns == 0) {
		// 60hz
		interval_ns = (1000 * 1000 * 1000) / 60;
	}

	s->display = -1;
	s->color_format = VK_FORMAT_B8G8R8A8_SRGB;
	s->color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	s->present_mode = VK_PRESENT_MODE_FIFO_KHR;
	s->window_type = WINDOW_AUTO;
	s->fullscreen = false;
	s->distortion_model = xdev->hmd->distortion.preferred;
	s->width = xdev->hmd->screens[0].w_pixels;
	s->height = xdev->hmd->screens[0].h_pixels;
	s->nominal_frame_interval_ns = interval_ns;
	s->print_spew = debug_get_bool_option_print_spew();
	s->print_debug = debug_get_bool_option_print_debug();
	s->print_modes = debug_get_bool_option_print_modes();
	s->gpu_index = debug_get_num_option_force_gpu_index();
	s->debug.wireframe = debug_get_bool_option_wireframe();
	s->desired_mode = debug_get_num_option_desired_mode();

	if (debug_get_bool_option_force_nvidia()) {
		s->window_type = WINDOW_DIRECT_NVIDIA;
	}
	if (debug_get_bool_option_force_randr()) {
		s->window_type = WINDOW_DIRECT_RANDR;
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
