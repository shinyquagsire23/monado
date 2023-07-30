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
XRT_MAYBE_UNUSED static const char *NV_DIRECT_ALLOWLIST[] = {
    "Sony SIE  HMD *08",           // PSVR
    "HTC Corporation HTC-VIVE",    // HTC Vive
    "HTC Corporation VIVE Pro",    // HTC Vive Pro
    "Oculus VR Inc. Rift",         // DK1, DK2 and CV1
    "Valve Corporation Index HMD", // Valve Index
    "Seiko/Epson SEC144A",         // Samsung Odyssey+
    "HPN",                         // Reverb G2
    "HP Inc.",                     // Also Reverb G2?
    "PNP",                         // NorthStar (Generic)
};

/*!
 * Settings for the compositor.
 *
 * @ingroup comp_main
 */
struct comp_settings
{
	int display;

	bool use_compute;

	VkFormat color_format;
	VkColorSpaceKHR color_space;
	VkPresentModeKHR present_mode;

	//! Preferred window type to use, not actual used.
	const char *target_identifier;

	//! display string forced by user or NULL
	const char *nvidia_display;

	//! vk display number to use when forcing vk_display
	int vk_display;

	struct
	{
		uint32_t width;
		uint32_t height;
	} preferred;

	//! Percentage to scale the viewport by.
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


	//! Vulkan device UUID selected by comp_settings_check_vulkan_caps, valid across Vulkan instances
	xrt_uuid_t selected_gpu_deviceUUID;

	//! Vulkan device UUID to suggest to clients
	xrt_uuid_t client_gpu_deviceUUID;

	//! The Windows LUID for the GPU device suggested for D3D clients, never changes.
	xrt_luid_t client_gpu_deviceLUID;

	//! Whether @ref client_gpu_deviceLUID is valid
	bool client_gpu_deviceLUID_valid;

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
