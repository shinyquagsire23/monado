// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helper for getting information from a VkSurfaceKHR.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_vk
 */

#pragma once

#include "vk/vk_helpers.h"


#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Struct(s).
 *
 */

struct vk_surface_info
{
	VkPresentModeKHR *present_modes;
	VkSurfaceFormatKHR *formats;

	uint32_t present_mode_count;
	uint32_t format_count;

	VkSurfaceCapabilitiesKHR caps;

#ifdef VK_EXT_display_surface_counter
	VkSurfaceCapabilities2EXT caps2;
#endif
};


/*
 *
 * Functions.
 *
 */

/*!
 * Free all lists allocated by @ref vk_surface_info.
 */
void
vk_surface_info_destroy(struct vk_surface_info *info);

/*!
 * Fill in the given @ref vk_surface_info, will allocate lists.
 */
XRT_CHECK_RESULT VkResult
vk_surface_info_fill_in(struct vk_bundle *vk, struct vk_surface_info *info, VkSurfaceKHR surface);

/*!
 * Print out the gathered information about the
 * surface given to @ref vk_surface_info_fill_in.
 */
void
vk_print_surface_info(struct vk_bundle *vk, struct vk_surface_info *info, enum u_logging_level log_level);


#ifdef __cplusplus
}
#endif
