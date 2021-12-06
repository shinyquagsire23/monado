// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helper implementation for native compositors.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_util
 */

#pragma once

#include "util/comp_sync.h"
#include "util/comp_swapchain.h"


#ifdef __cplusplus
extern "C" {
#endif

#define COMP_MAX_LAYERS 16

/*!
 * A single layer.
 *
 * @ingroup comp_util
 * @see comp_layer_slot
 */
struct comp_layer
{
	/*!
	 * Up to four compositor swapchains referenced per layer.
	 *
	 * Unused elements should be set to null.
	 */
	struct comp_swapchain *sc_array[4];

	/*!
	 * All basic (trivially-serializable) data associated with a layer.
	 */
	struct xrt_layer_data data;
};

/*!
 * A stack of layers.
 *
 * @ingroup comp_util
 * @see comp_base
 */
struct comp_layer_slot
{
	//! What environmental blend mode did the app use.
	enum xrt_blend_mode env_blend_mode;

	//! All of the layers.
	struct comp_layer layers[COMP_MAX_LAYERS];

	//! Number of submitted layers.
	uint32_t layer_count;

	//! Special case one layer projection/projection-depth fast-path.
	bool one_projection_layer_fast_path;
};

/*!
 * A simple compositor base that handles a lot of things for you.
 *
 * Things it handles for you:
 * * App swapchains
 * * App fences
 * * Vulkan bundle (needed for swapchains and fences)
 * * Layer tracking, not @ref xrt_compositor::layer_commit
 * * Wait function, not @ref xrt_compositor::predict_frame
 *
 * Functions it does not handle:
 * * @ref xrt_compositor::begin_session
 * * @ref xrt_compositor::end_session
 * * @ref xrt_compositor::predict_frame
 * * @ref xrt_compositor::mark_frame
 * * @ref xrt_compositor::begin_frame
 * * @ref xrt_compositor::discard_frame
 * * @ref xrt_compositor::layer_commit
 * * @ref xrt_compositor::poll_events
 * * @ref xrt_compositor::destroy
 *
 * @ingroup comp_util
 * @see comp_base
 */
struct comp_base
{
	//! Base native compositor.
	struct xrt_compositor_native base;

	//! Vulkan bundle of useful things, used by swapchain and fence.
	struct vk_bundle vk;

	//! For default @ref xrt_compositor::wait_frame.
	struct os_precise_sleeper sleeper;

	//! Swapchain garbage collector, used by swapchain, child class needs to call.
	struct comp_swapchain_gc cscgc;

	//! We only need to track a single slot.
	struct comp_layer_slot slot;
};


/*
 *
 * Helper functions.
 *
 */

/*!
 * Convenience function to convert a xrt_compositor to a comp_base.
 *
 * @private @memberof comp_base
 */
static inline struct comp_base *
comp_base(struct xrt_compositor *xc)
{
	return (struct comp_base *)xc;
}


/*
 *
 * 'Exported' functions.
 *
 */

/*!
 * Inits all of the supported functions and structs, except @ref vk_bundle.
 *
 * The bundle needs to be initialised before any of the implemented functions
 * are call, but is not required to be initialised before this function is
 * called.
 */
void
comp_base_init(struct comp_base *cb);

/*!
 * De-initialises all structs, except @ref vk_bundle.
 *
 * The bundle needs to be de-initialised by the sub-class.
 */
void
comp_base_fini(struct comp_base *cb);


#ifdef __cplusplus
}
#endif
