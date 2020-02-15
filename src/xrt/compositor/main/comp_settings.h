// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Settings struct for compositor header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_vulkan_includes.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Since NVidia direct mode lets us 'acquire' any display, we need to
 * be careful about which displays we attempt to acquire.
 * We may wish to allow user configuration to extend this list.
 */
XRT_MAYBE_UNUSED static const char *NV_DIRECT_WHITELIST[] = {
    "Sony SIE  HMD *08",
    "HTC Corporation HTC-VIVE",
};

/*!
 * Window type to use.
 *
 * @ingroup comp
 */
enum window_type
{
	WINDOW_NONE = 0,
	WINDOW_AUTO,
	WINDOW_XCB,
	WINDOW_WAYLAND,
	WINDOW_DIRECT_RANDR,
	WINDOW_DIRECT_NVIDIA,
};


/*!
 * Settings for the compositor.
 *
 * @ingroup comp
 */
struct comp_settings
{
	int display;

	VkFormat color_format;
	VkColorSpaceKHR color_space;
	VkPresentModeKHR present_mode;

	//! Window type to use.
	enum window_type window_type;

	//! Distortion type to use.
	enum xrt_distortion_model distortion_model;

	uint32_t width;
	uint32_t height;

	struct
	{
		//! Display wireframe instead of solid triangles.
		bool wireframe;
	} debug;

	//! Not used with direct mode.
	bool fullscreen;

	//! Should we debug print a lot!
	bool print_spew;

	//! Should we debug print.
	bool print_debug;

	//! Print information about available modes for direct mode.
	bool print_modes;

	//! Should we flip y axis for compositor buffers (for GL)
	bool flip_y;

	//! Nominal frame interval
	uint64_t nominal_frame_interval_ns;

	//! Enable vulkan validation for compositor
	bool validate_vulkan;

	//! Run the compositor on this Vulkan physical device
	int gpu_index;

	//! Try to choose the mode with this index for direct mode
	int desired_mode;
};

/*!
 * Initialize the settings struct with either defaults or loaded setting.
 *
 * @ingroup comp
 */
void
comp_settings_init(struct comp_settings *s, struct xrt_device *xdev);


#ifdef __cplusplus
}
#endif
