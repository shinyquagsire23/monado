// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Independent semaphore implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_util
 */

#pragma once

#include "xrt/xrt_compositor.h"
#include "vk/vk_helpers.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * A simple implementation of the xrt_compositor_semaphore interface.
 *
 * @ingroup comp_util
 * @implements xrt_compositor_swapchain
 * @see comp_compositor
 */
struct comp_semaphore
{
	struct xrt_compositor_semaphore base;

	struct vk_bundle *vk;

	VkSemaphore semaphore;
};


/*
 *
 * Helper functions.
 *
 */

/*!
 * Convenience function to convert a xrt_compositor_semaphore to a comp_semaphore.
 *
 * @ingroup comp_util
 * @private @memberof comp_semaphore
 */
static inline struct comp_semaphore *
comp_semaphore(struct xrt_compositor_semaphore *xcsem)
{
	return (struct comp_semaphore *)xcsem;
}


/*
 *
 * 'Exported' functions.
 *
 */

/*!
 * Creates a @ref comp_semaphore, used to implement compositor functionality.
 *
 * @ingroup comp_util
 */
xrt_result_t
comp_semaphore_create(struct vk_bundle *vk,
                      xrt_graphics_sync_handle_t *out_handle,
                      struct xrt_compositor_semaphore **out_xcsem);


#ifdef __cplusplus
}
#endif
