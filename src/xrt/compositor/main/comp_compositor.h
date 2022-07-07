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
#include "util/u_frame_times_widget.h"

#include "util/comp_base.h"
#include "util/comp_sync.h"
#include "util/comp_swapchain.h"

#include "render/render_interface.h"

#include "main/comp_target.h"
#include "main/comp_window.h"
#include "main/comp_settings.h"
#include "main/comp_renderer.h"

struct comp_window_peek;

#ifdef __cplusplus
extern "C" {
#endif

#define COMP_MAX_LAYERS 16

/*
 *
 * Structs
 *
 */

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
	struct comp_base base;

	//! Renderer helper.
	struct comp_renderer *r;

	//! The target we are displaying to.
	struct comp_target *target;

	//! The device we are displaying to.
	struct xrt_device *xdev;

	//! The settings.
	struct comp_settings settings;

	//! Vulkan shaders that the compositor uses.
	struct render_shaders shaders;

	//! Timestamp of last-rendered (immersive) frame.
	int64_t last_frame_time_ns;

	//! State for generating the correct set of events.
	enum comp_state state;

	// Extents of one view, in pixels.
	VkExtent2D view_extents;

	//! Are we mirroring any of the views to the debug gui? If so, turn off the fast path.
	bool mirroring_to_debug_gui;

	//! On screen window to display the content of the HMD.
	struct comp_window_peek *peek;

	/*!
	 * @brief Data exclusive to the begin_frame/end_frame for computing an
	 * estimate of the app's needs.
	 */
	struct
	{
		int64_t last_begin;
		int64_t last_end;
	} app_profiling;

	struct u_frame_times_widget compositor_frame_times;

	struct
	{
		struct comp_frame waited;
		struct comp_frame rendering;
	} frame;

	struct
	{
		//! Temporarily disable ATW
		bool atw_off;
	} debug;

	struct render_resources nr;
};


/*
 *
 * Functions and helpers.
 *
 */

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
