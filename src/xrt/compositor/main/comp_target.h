// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Abstracted compositor rendering target.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_defines.h"

#include "vk/vk_helpers.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Image and view pair for @ref comp_target.
 *
 * @ingroup comp_main
 */
struct comp_target_image
{
	VkImage handle;
	VkImageView view;
};

/*!
 * A target is essentially a swapchain, but it is such a overloaded term so
 * we are differencating swapchains that the compositor provides to clients and
 * swapchains that the compositor renders by naming the latter to target.
 *
 * @ingroup comp_main
 */
struct comp_target
{
	//! Owning compositor.
	struct comp_compositor *c;

	//! Name of the backing system.
	const char *name;

	//! Current dimensions of the target.
	uint32_t width, height;

	//! The format that the renderpass targeting this target should use.
	VkFormat format;

	//! Number of images that this target has.
	uint32_t num_images;
	//! Array of images and image views for rendering.
	struct comp_target_image *images;

	//! Transformation of the current surface, required for pre-rotation
	VkSurfaceTransformFlagBitsKHR surface_transform;

	/*!
	 * Do any initialization that is required to happen before Vulkan has
	 * been loaded.
	 */
	bool (*init_pre_vulkan)(struct comp_target *ct);

	/*!
	 * Do any initialization that requires Vulkan to be loaded, you need to
	 * call @ref create_images after calling this function.
	 */
	bool (*init_post_vulkan)(struct comp_target *ct, uint32_t preferred_width, uint32_t preferred_height);

	/*!
	 * Create or recreate the image(s) of the target, for swapchain based
	 * targets this will (re)create the swapchain.
	 */
	void (*create_images)(struct comp_target *ct,
	                      uint32_t preferred_width,
	                      uint32_t preferred_height,
	                      VkFormat preferred_color_format,
	                      VkColorSpaceKHR preferred_color_space,
	                      VkPresentModeKHR present_mode);

	/*!
	 * Acquire the next image for rendering.
	 */
	VkResult (*acquire)(struct comp_target *ct, VkSemaphore semaphore, uint32_t *out_index);

	/*!
	 * Present the image at index to the screen.
	 */
	VkResult (*present)(struct comp_target *ct, VkQueue queue, uint32_t index, VkSemaphore semaphore);

	/*!
	 * Flush any WSI state before rendering.
	 */
	void (*flush)(struct comp_target *ct);

	/*!
	 * If the target can show a title (like a window) set the title.
	 */
	void (*set_title)(struct comp_target *ct, const char *title);

	/*!
	 * Destroys this target.
	 */
	void (*destroy)(struct comp_target *ct);
};

/*!
 * @copydoc comp_target::init_pre_vulkan
 *
 * @public @memberof comp_target
 * @ingroup comp_main
 */
static inline bool
comp_target_init_pre_vulkan(struct comp_target *ct)
{
	return ct->init_pre_vulkan(ct);
}

/*!
 * @copydoc comp_target::init_post_vulkan
 *
 * @public @memberof comp_target
 * @ingroup comp_main
 */
static inline bool
comp_target_init_post_vulkan(struct comp_target *ct, uint32_t preferred_width, uint32_t preferred_height)
{
	return ct->init_post_vulkan(ct, preferred_width, preferred_height);
}

/*!
 * @copydoc comp_target::create_images
 *
 * @public @memberof comp_target
 * @ingroup comp_main
 */
static inline void
comp_target_create_images(struct comp_target *ct,
                          uint32_t preferred_width,
                          uint32_t preferred_height,
                          VkFormat preferred_color_format,
                          VkColorSpaceKHR preferred_color_space,
                          VkPresentModeKHR present_mode)
{
	ct->create_images(ct, preferred_width, preferred_height, preferred_color_format, preferred_color_space,
	                  present_mode);
}

/*!
 * @copydoc comp_target::acquire
 *
 * @public @memberof comp_target
 * @ingroup comp_main
 */
static inline VkResult
comp_target_acquire(struct comp_target *ct, VkSemaphore semaphore, uint32_t *out_index)
{
	return ct->acquire(ct, semaphore, out_index);
}

/*!
 * @copydoc comp_target::present
 *
 * @public @memberof comp_target
 * @ingroup comp_main
 */
static inline VkResult
comp_target_present(struct comp_target *ct, VkQueue queue, uint32_t index, VkSemaphore semaphore)

{
	return ct->present(ct, queue, index, semaphore);
}

/*!
 * @copydoc comp_target::flush
 *
 * @public @memberof comp_target
 * @ingroup comp_main
 */
static inline void
comp_target_flush(struct comp_target *ct)
{
	ct->flush(ct);
}

/*!
 * @copydoc comp_target::set_title
 *
 * @public @memberof comp_target
 * @ingroup comp_main
 */
static inline void
comp_target_set_title(struct comp_target *ct, const char *title)
{
	ct->set_title(ct, title);
}

/*!
 * @copydoc comp_target::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * ct_ptr to null if freed.
 *
 * @public @memberof comp_target
 * @ingroup comp_main
 */
static inline void
comp_target_destroy(struct comp_target **ct_ptr)
{
	struct comp_target *ct = *ct_ptr;
	if (ct == NULL) {
		return;
	}

	ct->destroy(ct);
	*ct_ptr = NULL;
}


#ifdef __cplusplus
}
#endif
