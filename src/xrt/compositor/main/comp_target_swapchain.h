// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Target Vulkan swapchain code header.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "vk/vk_helpers.h"

#include "main/comp_target.h"


#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Structs.
 *
 */

struct u_frame_timing;

/*!
 * Wraps and manage VkSwapchainKHR and VkSurfaceKHR, used by @ref comp code.
 *
 * @ingroup comp_main
 */
struct comp_target_swapchain
{
	//! Base target.
	struct comp_target base;

	//! Frame timing tracker.
	struct u_frame_timing *uft;

	//! Also works as a frame index.
	int64_t current_frame_id;

	struct
	{
		VkSwapchainKHR handle;
	} swapchain;

	struct
	{
		VkSurfaceKHR handle;
		VkSurfaceFormatKHR format;
	} surface;

	struct
	{
		VkFormat color_format;
		VkColorSpaceKHR color_space;
	} preferred;

	//! Present mode that the system must support.
	VkPresentModeKHR present_mode;
};


/*
 *
 * Functions.
 *
 */

/*!
 * Pre Vulkan initialisation, sets function pointers.
 *
 * @ingroup comp_main
 */
void
comp_target_swapchain_init_set_fnptrs(struct comp_target_swapchain *cts);

/*!
 * Free all managed resources on the given @ref comp_target_swapchain,
 * does not free the struct itself.
 *
 * @ingroup comp_main
 */
void
comp_target_swapchain_cleanup(struct comp_target_swapchain *cts);


#ifdef __cplusplus
}
#endif
