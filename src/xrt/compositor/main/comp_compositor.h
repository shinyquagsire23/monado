// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main compositor written using Vulkan header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp_main
 */

#pragma once

#include "xrt/xrt_gfx_vk.h"
#include "xrt/xrt_config_build.h"

#include "util/u_threading.h"
#include "util/u_index_fifo.h"
#include "util/u_logging.h"

#include "vk/vk_image_allocator.h"

#include "main/comp_sync.h"
#include "main/comp_settings.h"
#include "main/comp_swapchain.h"
#include "main/comp_window.h"
#include "main/comp_renderer.h"
#include "main/comp_target.h"

#include "render/comp_render.h"


#ifdef __cplusplus
extern "C" {
#endif

#define NUM_FRAME_TIMES 50
#define COMP_MAX_LAYERS 16

/*
 *
 * Structs
 *
 */

/*!
 * A single layer.
 *
 * @ingroup comp_main
 * @see comp_layer_slot
 */
struct comp_layer
{
	/*!
	 * Up to two compositor swapchains referenced per layer.
	 *
	 * Unused elements should be set to null.
	 */
	struct comp_swapchain *scs[2];

	/*!
	 * All basic (trivially-serializable) data associated with a layer.
	 */
	struct xrt_layer_data data;
};

/*!
 * A stack of layers.
 *
 * @ingroup comp_main
 * @see comp_compositor
 */
struct comp_layer_slot
{
	enum xrt_blend_mode env_blend_mode;

	struct comp_layer layers[COMP_MAX_LAYERS];

	uint32_t num_layers;
};

/*!
 * State to emulate state transitions correctly.
 *
 * @ingroup comp_main
 */
enum comp_state
{
	COMP_STATE_UNINITIALIZED = 0,
	COMP_STATE_READY = 1,
	COMP_STATE_PREPARED = 2,
	COMP_STATE_VISIBLE = 3,
	COMP_STATE_FOCUSED = 4,
};


/*!
 * Tracking frame state.
 */
struct comp_frame
{
	int64_t id;
	uint64_t predicted_display_time_ns;
	uint64_t desired_present_time_ns;
	uint64_t present_slop_ns;
};

/*!
 * Main compositor struct tying everything in the compositor together.
 *
 * @ingroup comp_main
 * @implements xrt_compositor_native
 */
struct comp_compositor
{
	struct xrt_compositor_native base;

	//! Renderer helper.
	struct comp_renderer *r;

	//! The target we are displaying to.
	struct comp_target *target;

	//! The device we are displaying to.
	struct xrt_device *xdev;

	//! The settings.
	struct comp_settings settings;

	//! Vulkan bundle of things.
	struct vk_bundle vk;

	//! Vulkan shaders that the compositor uses.
	struct comp_shaders shaders;

	//! Timestamp of last-rendered (immersive) frame.
	int64_t last_frame_time_ns;

	//! State for generating the correct set of events.
	enum comp_state state;

	struct os_precise_sleeper sleeper;

	//! Triple buffered layer stacks.
	struct comp_layer_slot slots[3];

	/*!
	 * @brief Data exclusive to the begin_frame/end_frame for computing an
	 * estimate of the app's needs.
	 */
	struct
	{
		int64_t last_begin;
		int64_t last_end;
	} app_profiling;

	struct
	{
		//! Current Index for times_ns.
		int index;

		//! Timestamps of last-rendered (immersive) frames.
		int64_t times_ns[NUM_FRAME_TIMES];

		//! Frametimes between last-rendered (immersive) frames.
		float timings_ms[NUM_FRAME_TIMES];

		//! Average FPS of last NUM_FRAME_TIMES rendered frames.
		float fps;

		struct u_var_timing *debug_var;
	} compositor_frame_times;

	struct
	{
		struct comp_frame waited;
		struct comp_frame rendering;
	} frame;

	struct comp_swapchain_gc cscgc;

	struct
	{
		//! Temporarily disable ATW
		bool atw_off;
	} debug;

	struct comp_resources nr;
};


/*
 *
 * Functions and helpers.
 *
 */

/*!
 * Check if the compositor can create swapchains with this format.
 */
bool
comp_is_format_supported(struct comp_compositor *c, VkFormat format);

/*!
 * Convenience function to convert a xrt_compositor to a comp_compositor.
 *
 * @private @memberof comp_compositor
 */
static inline struct comp_compositor *
comp_compositor(struct xrt_compositor *xc)
{
	return (struct comp_compositor *)xc;
}

/*!
 * Spew level logging.
 *
 * @relates comp_compositor
 */
#define COMP_SPEW(c, ...) U_LOG_IFL_T(c->settings.log_level, __VA_ARGS__);

/*!
 * Debug level logging.
 *
 * @relates comp_compositor
 */
#define COMP_DEBUG(c, ...) U_LOG_IFL_D(c->settings.log_level, __VA_ARGS__);

/*!
 * Info level logging.
 *
 * @relates comp_compositor
 */
#define COMP_INFO(c, ...) U_LOG_IFL_I(c->settings.log_level, __VA_ARGS__);

/*!
 * Warn level logging.
 *
 * @relates comp_compositor
 */
#define COMP_WARN(c, ...) U_LOG_IFL_W(c->settings.log_level, __VA_ARGS__);

/*!
 * Error level logging.
 *
 * @relates comp_compositor
 */
#define COMP_ERROR(c, ...) U_LOG_IFL_E(c->settings.log_level, __VA_ARGS__);

/*!
 * Mode printing.
 *
 * @relates comp_compositor
 */
#define COMP_PRINT_MODE(c, ...)                                                                                        \
	if (c->settings.print_modes) {                                                                                 \
		U_LOG_I(__VA_ARGS__);                                                                                  \
	}


#ifdef __cplusplus
}
#endif
