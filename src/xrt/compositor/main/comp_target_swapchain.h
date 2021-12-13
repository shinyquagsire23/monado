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

	//! If we should use display timing.
	enum comp_target_display_timing_usage timing_usage;

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

	//! The current display used for direct mode, VK_NULL_HANDLE else.
	VkDisplayKHR display;
};


/*
 *
 * Functions.
 *
 */

/*!
 * @brief Pre Vulkan initialisation, sets function pointers.
 *
 * Call from the creation function for your "subclass", after allocating.
 *
 * Initializes these function pointers, all other methods of @ref comp_target are the responsibility of the caller (the
 * "subclass"):
 *
 * - comp_target::check_ready
 * - comp_target::create_images
 * - comp_target::has_images
 * - comp_target::acquire
 * - comp_target::present
 * - comp_target::calc_frame_timings
 * - comp_target::mark_timing_point
 * - comp_target::update_timings
 *
 * Also sets comp_target_swapchain::timing_usage to the provided value.
 *
 * @protected @memberof comp_target_swapchain
 *
 * @ingroup comp_main
 */
void
comp_target_swapchain_init_and_set_fnptrs(struct comp_target_swapchain *cts,
                                          enum comp_target_display_timing_usage timing_usage);

/*!
 * Free all managed resources on the given @ref comp_target_swapchain,
 * does not free the struct itself.
 *
 * @protected @memberof comp_target_swapchain
 *
 * @ingroup comp_main
 */
void
comp_target_swapchain_cleanup(struct comp_target_swapchain *cts);


#ifdef __cplusplus
}
#endif
