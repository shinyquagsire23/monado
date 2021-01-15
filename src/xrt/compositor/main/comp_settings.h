// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Settings struct for compositor header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_vulkan_includes.h"

#include "util/u_logging.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Since NVidia direct mode lets us 'acquire' any display, we need to
 * be careful about which displays we attempt to acquire.
 * We may wish to allow user configuration to extend this list.
 */
XRT_MAYBE_UNUSED static const char *NV_DIRECT_WHITELIST[] = {
    "Sony SIE  HMD *08",           "HTC Corporation HTC-VIVE",
    "HTC Corporation VIVE Pro",    "Oculus VR Inc. Rift", /* Matches DK1, DK2 and CV1 */
    "Valve Corporation Index HMD",
};

/*!
 * Window type to use.
 *
 * @ingroup comp_main
 */
enum window_type
{
	WINDOW_NONE = 0,
	WINDOW_AUTO,
	WINDOW_XCB,
	WINDOW_WAYLAND,
	WINDOW_DIRECT_RANDR,
	WINDOW_DIRECT_NVIDIA,
	WINDOW_ANDROID,
	WINDOW_MSWIN,
	WINDOW_VK_DISPLAY,
};


/*!
 * Settings for the compositor.
 *
 * @ingroup comp_main
 */
struct comp_settings
{
	int display;

	VkFormat color_format;
	VkColorSpaceKHR color_space;
	VkPresentModeKHR present_mode;

	//! Window type to use.
	enum window_type window_type;

	//! display string forced by user or NULL
	const char *nvidia_display;

	//! vk display number to use when forcing vk_display
	int vk_display;

	struct
	{
		uint32_t width;
		uint32_t height;
	} preferred;

	struct
	{
		//! Display wireframe instead of solid triangles.
		bool wireframe;
	} debug;

	//! Procentage to scale the viewport by.
	double viewport_scale;

	//! Not used with direct mode.
	bool fullscreen;

	//! Logging level.
	enum u_logging_level log_level;

	//! Print information about available modes for direct mode.
	bool print_modes;

	//! Nominal frame interval
	uint64_t nominal_frame_interval_ns;

	//! Vulkan physical device selected by comp_settings_check_vulkan_caps
	//! may be forced by user
	int selected_gpu_index;

	//! Vulkan physical device index for clients to use, forced by user
	int client_gpu_index;


	//! Vulkan device UUID selected by comp_settings_check_vulkan_caps,
	//! valid across Vulkan instances
	uint8_t selected_gpu_deviceUUID[XRT_GPU_UUID_SIZE];

	//! Vulkan device UUID to suggest to clients
	uint8_t client_gpu_deviceUUID[XRT_GPU_UUID_SIZE];

	//! Try to choose the mode with this index for direct mode
	int desired_mode;
};

/*!
 * Initialize the settings struct with either defaults or loaded setting.
 *
 * @ingroup comp_main
 */
void
comp_settings_init(struct comp_settings *s, struct xrt_device *xdev);


#ifdef __cplusplus
}
#endif
