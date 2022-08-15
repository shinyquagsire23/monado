// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Independent swapchain implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_util
 */

#pragma once

#include "vk/vk_image_allocator.h"

#include "util/u_threading.h"
#include "util/u_index_fifo.h"


#ifdef __cplusplus
extern "C" {
#endif


struct comp_swapchain;

/*!
 * Callback for implementing own destroy function, should call
 * @ref comp_swapchain_teardown and is responsible for memory.
 *
 * @ingroup comp_util
 */
typedef void (*comp_swapchain_destroy_func_t)(struct comp_swapchain *sc);

/*!
 * A garbage collector that collects swapchains to be safely destroyed.
 *
 * @ingroup comp_util
 */
struct comp_swapchain_gc
{
	//! Thread object for safely destroying swapchain.
	struct u_threading_stack destroy_swapchains;
};

/*!
 * A single swapchain image, holds the needed state for tracking image usage.
 *
 * @ingroup comp_util
 * @see comp_swapchain
 */
struct comp_swapchain_image
{
	//! Sampler used by the renderer and distortion code.
	VkSampler sampler;
	VkSampler repeat_sampler;
	//! Views used by the renderer and distortion code, for each array layer.
	struct
	{
		VkImageView *alpha;
		VkImageView *no_alpha;
	} views;
	//! The number of array slices in a texture, 1 == regular 2D texture.
	size_t array_size;
};

/*!
 * A swapchain that is almost a one to one mapping to a OpenXR swapchain.
 *
 * Not used by the window backend that uses the comp_target to render to.
 *
 * The vk_bundle is owned by the compositor, its the state trackers job to make
 * sure that compositor lives for as long as the swapchain does and that all
 * swapchains are destroyed before the compositor is destroyed.
 *
 * @ingroup comp_util
 * @implements xrt_swapchain_native
 * @see comp_compositor
 */
struct comp_swapchain
{
	struct xrt_swapchain_native base;

	struct vk_bundle *vk;
	struct comp_swapchain_gc *gc;

	struct vk_image_collection vkic;
	struct comp_swapchain_image images[XRT_MAX_SWAPCHAIN_IMAGES];

	/*!
	 * This fifo is used to always give out the oldest image to acquire
	 * image, this should probably be made even smarter.
	 */
	struct u_index_fifo fifo;

	//! Virtual real destroy function.
	comp_swapchain_destroy_func_t real_destroy;
};


/*
 *
 * Helper functions.
 *
 */

/*!
 * Convenience function to convert a xrt_swapchain to a comp_swapchain.
 *
 * @ingroup comp_util
 * @private @memberof comp_swapchain
 */
static inline struct comp_swapchain *
comp_swapchain(struct xrt_swapchain *xsc)
{
	return (struct comp_swapchain *)xsc;
}


/*
 *
 * 'Exported' parent-class functions.
 *
 */

/*!
 * Helper to init a comp_swachain struct as if it was a create operation,
 * useful for wrapping comp_swapchain within another struct. Ref-count is
 * set to zero so the caller need to init it correctly.
 *
 * @ingroup comp_util
 */
xrt_result_t
comp_swapchain_create_init(struct comp_swapchain *sc,
                           comp_swapchain_destroy_func_t destroy_func,
                           struct vk_bundle *vk,
                           struct comp_swapchain_gc *cscgc,
                           const struct xrt_swapchain_create_info *info,
                           const struct xrt_swapchain_create_properties *xsccp);

/*!
 * Helper to init a comp_swachain struct as if it was a import operation,
 * useful for wrapping comp_swapchain within another struct. Ref-count is
 * set to zero so the caller need to init it correctly.
 *
 * @ingroup comp_util
 */
xrt_result_t
comp_swapchain_import_init(struct comp_swapchain *sc,
                           comp_swapchain_destroy_func_t destroy_func,
                           struct vk_bundle *vk,
                           struct comp_swapchain_gc *cscgc,
                           const struct xrt_swapchain_create_info *info,
                           struct xrt_image_native *native_images,
                           uint32_t native_image_count);

/*!
 * De-inits a comp_swapchain, usable for classes sub-classing comp_swapchain.
 *
 * @ingroup comp_util
 */
void
comp_swapchain_teardown(struct comp_swapchain *sc);


/*
 *
 * 'Exported' garbage collection functions.
 *
 */

/*!
 * Do garbage collection, destroying any resources that has been scheduled for
 * destruction from other threads.
 *
 * @ingroup comp_util
 */
void
comp_swapchain_garbage_collect(struct comp_swapchain_gc *cscgc);


/*
 *
 * 'Exported' default implementation.
 *
 */

/*!
 * A compositor function that is implemented in the swapchain code.
 *
 * @ingroup comp_util
 */
xrt_result_t
comp_swapchain_get_create_properties(const struct xrt_swapchain_create_info *info,
                                     struct xrt_swapchain_create_properties *xsccp);

/*!
 * A compositor function that is implemented in the swapchain code.
 *
 * @ingroup comp_util
 */
xrt_result_t
comp_swapchain_create(struct vk_bundle *vk,
                      struct comp_swapchain_gc *cscgc,
                      const struct xrt_swapchain_create_info *info,
                      const struct xrt_swapchain_create_properties *xsccp,
                      struct xrt_swapchain **out_xsc);

/*!
 * A compositor function that is implemented in the swapchain code.
 *
 * @ingroup comp_util
 */
xrt_result_t
comp_swapchain_import(struct vk_bundle *vk,
                      struct comp_swapchain_gc *cscgc,
                      const struct xrt_swapchain_create_info *info,
                      struct xrt_image_native *native_images,
                      uint32_t image_count,
                      struct xrt_swapchain **out_xsc);


#ifdef __cplusplus
}
#endif
