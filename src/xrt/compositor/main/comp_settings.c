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
DEBUG_GET_ONCE_LOG_OPTION(log, "XRT_COMPOSITOR_LOG", U_LOGGING_WARN)
DEBUG_GET_ONCE_BOOL_OPTION(print_modes, "XRT_COMPOSITOR_PRINT_MODES", false)
DEBUG_GET_ONCE_BOOL_OPTION(force_randr, "XRT_COMPOSITOR_FORCE_RANDR", false)
DEBUG_GET_ONCE_BOOL_OPTION(force_nvidia, "XRT_COMPOSITOR_FORCE_NVIDIA", false)
DEBUG_GET_ONCE_OPTION(nvidia_display, "XRT_COMPOSITOR_FORCE_NVIDIA_DISPLAY", NULL)
DEBUG_GET_ONCE_NUM_OPTION(vk_display, "XRT_COMPOSITOR_FORCE_VK_DISPLAY", -1)
DEBUG_GET_ONCE_BOOL_OPTION(force_xcb, "XRT_COMPOSITOR_FORCE_XCB", false)
DEBUG_GET_ONCE_BOOL_OPTION(force_wayland, "XRT_COMPOSITOR_FORCE_WAYLAND", false)
DEBUG_GET_ONCE_BOOL_OPTION(wireframe, "XRT_COMPOSITOR_WIREFRAME", false)
DEBUG_GET_ONCE_NUM_OPTION(force_gpu_index, "XRT_COMPOSITOR_FORCE_GPU_INDEX", -1)
DEBUG_GET_ONCE_NUM_OPTION(force_client_gpu_index, "XRT_COMPOSITOR_FORCE_CLIENT_GPU_INDEX", -1)
DEBUG_GET_ONCE_NUM_OPTION(desired_mode, "XRT_COMPOSITOR_DESIRED_MODE", -1)
DEBUG_GET_ONCE_NUM_OPTION(scale_percentage, "XRT_COMPOSITOR_SCALE_PERCENTAGE", 140)
DEBUG_GET_ONCE_BOOL_OPTION(xcb_fullscreen, "XRT_COMPOSITOR_XCB_FULLSCREEN", false)
DEBUG_GET_ONCE_NUM_OPTION(xcb_display, "XRT_COMPOSITOR_XCB_DISPLAY", -1)
DEBUG_GET_ONCE_NUM_OPTION(default_framerate, "XRT_COMPOSITOR_DEFAULT_FRAMERATE", 60)
// clang-format on

void
comp_settings_init(struct comp_settings *s, struct xrt_device *xdev)
{
	int default_framerate = debug_get_num_option_default_framerate();

	uint64_t interval_ns = xdev->hmd->screens[0].nominal_frame_interval_ns;
	if (interval_ns == 0) {
		interval_ns = (1000 * 1000 * 1000) / default_framerate;
	}

	s->display = debug_get_num_option_xcb_display();
	s->color_format = VK_FORMAT_B8G8R8A8_SRGB;
	s->color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	s->present_mode = VK_PRESENT_MODE_FIFO_KHR;
	s->window_type = WINDOW_AUTO;
	s->fullscreen = debug_get_bool_option_xcb_fullscreen();
	s->preferred.width = xdev->hmd->screens[0].w_pixels;
	s->preferred.height = xdev->hmd->screens[0].h_pixels;
	s->nominal_frame_interval_ns = interval_ns;
	s->log_level = debug_get_log_option_log();
	s->print_modes = debug_get_bool_option_print_modes();
	s->selected_gpu_index = debug_get_num_option_force_gpu_index();
	s->client_gpu_index = debug_get_num_option_force_client_gpu_index();
	s->debug.wireframe = debug_get_bool_option_wireframe();
	s->desired_mode = debug_get_num_option_desired_mode();
	s->viewport_scale = debug_get_num_option_scale_percentage() / 100.0;

	if (debug_get_bool_option_force_nvidia()) {
		s->window_type = WINDOW_DIRECT_NVIDIA;
	}

	s->nvidia_display = debug_get_option_nvidia_display();
	s->vk_display = debug_get_num_option_vk_display();
	if (s->vk_display >= 0) {
		s->window_type = WINDOW_VK_DISPLAY;
	}

	if (debug_get_bool_option_force_randr()) {
		s->window_type = WINDOW_DIRECT_RANDR;
	}

	if (debug_get_bool_option_force_xcb()) {
		s->window_type = WINDOW_XCB;
		// HMD screen tends to be much larger then monitors.
		s->preferred.width /= 2;
		s->preferred.height /= 2;
	}
	if (debug_get_bool_option_force_wayland()) {
		s->window_type = WINDOW_WAYLAND;
		// HMD screen tends to be much larger then monitors.
		s->preferred.width /= 2;
		s->preferred.height /= 2;
	}
}
