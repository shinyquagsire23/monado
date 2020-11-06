// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor window header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#include "main/comp_window.h"


static inline struct comp_window *
comp_window(struct comp_target *ct)
{
	return (struct comp_window *)ct;
}

void
wt_create_images(struct comp_target *ct,
                 uint32_t preferred_width,
                 uint32_t preferred_height,
                 VkFormat preferred_color_format,
                 VkColorSpaceKHR preferred_color_space,
                 VkPresentModeKHR present_mode)
{
	struct comp_window *cw = comp_window(ct);

	vk_swapchain_create(        //
	    &cw->swapchain,         //
	    preferred_width,        //
	    preferred_height,       //
	    preferred_color_format, //
	    preferred_color_space,  //
	    present_mode);          //
}

VkResult
wt_acquire(struct comp_target *ct, VkSemaphore semaphore, uint32_t *out_index)
{
	struct comp_window *cw = comp_window(ct);

	return vk_swapchain_acquire_next_image( //
	    &cw->swapchain,                     //
	    semaphore,                          //
	    out_index);                         //
}

VkResult
wt_present(struct comp_target *ct,
           VkQueue queue,
           uint32_t index,
           VkSemaphore semaphore)
{
	struct comp_window *cw = comp_window(ct);

	return vk_swapchain_present( //
	    &cw->swapchain,          //
	    queue,                   //
	    index,                   //
	    semaphore);              //
}

void
comp_window_init_target(struct comp_window *wt)
{
	wt->swapchain.base.create_images = wt_create_images;
	wt->swapchain.base.acquire = wt_acquire;
	wt->swapchain.base.present = wt_present;
}
