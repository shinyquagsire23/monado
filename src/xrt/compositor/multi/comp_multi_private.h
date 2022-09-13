// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  System compositor capable of supporting multiple clients: internal structs.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_multi
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_compositor.h"

#include "os/os_time.h"
#include "os/os_threading.h"

#include "util/u_pacing.h"

#ifdef __cplusplus
extern "C" {
#endif


#define MULTI_MAX_CLIENTS 64
#define MULTI_MAX_LAYERS 16


/*
 *
 * Native compositor.
 *
 */

/*!
 * Data for a single composition layer.
 *
 * Similar in function to @ref comp_layer
 *
 * @ingroup comp_multi
 */
struct multi_layer_entry
{
	/*!
	 * Device to get pose from.
	 */
	struct xrt_device *xdev;

	/*!
	 * Pointers to swapchains.
	 *
	 * How many are actually used depends on the value of @p data.type
	 */
	struct xrt_swapchain *xscs[4];

	/*!
	 * All basic (trivially-serializable) data associated with a layer,
	 * aside from which swapchain(s) are used.
	 */
	struct xrt_layer_data data;
};

/*!
 * Render state for a single client, including all layers.
 *
 * @ingroup comp_multi
 */
struct multi_layer_slot
{
	uint64_t display_time_ns; //!< When should this be shown, @see XrFrameEndInfo::displayTime.
	enum xrt_blend_mode env_blend_mode;
	uint32_t layer_count;
	struct multi_layer_entry layers[MULTI_MAX_LAYERS];
	bool active;
};

/*!
 * Render state for a single client, including all layers.
 *
 * @ingroup comp_multi
 */
struct multi_event
{
	struct multi_event *next;
	union xrt_compositor_event xce;
};

/*!
 * A single compositor for feeding the layers from one session/app into
 * the multi-client-capable system compositor.
 *
 * @ingroup comp_multi
 */
struct multi_compositor
{
	struct xrt_compositor_native base;

	// Client info.
	struct xrt_session_info xsi;

	//! Owning system compositor.
	struct multi_system_compositor *msc;

	//! Used to implement wait frame, only used for in process.
	struct os_precise_sleeper frame_sleeper;

	//! Used when waiting for the scheduled frame to complete.
	struct os_precise_sleeper scheduled_sleeper;

	struct
	{
		struct os_mutex mutex;
		struct multi_event *next;
	} event;

	struct
	{
		bool visible;
		bool focused;

		int64_t z_order;

		bool session_active;
	} state;

	struct
	{
		//! Fence to wait for.
		struct xrt_compositor_fence *xcf;

		//! Timeline semaphore to wait for.
		struct xrt_compositor_semaphore *xcsem;

		//! Timeline semaphore value to wait for.
		uint64_t value;

		//! Frame id of frame being waited on.
		int64_t frame_id;

		//! The wait thread itself
		struct os_thread_helper oth;

		//! Have we gotten to the loop?
		bool alive;

		//! Is the thread waiting, if so the client should block.
		bool waiting;

		/*!
		 * Is the client thread blocked?
		 *
		 * Set to true by the client thread,
		 * cleared by the wait thread to release the client thread.
		 */
		bool blocked;
	} wait_thread;

	//! Lock for all of the slots.
	struct os_mutex slot_lock;

	/*!
	 * The next which the next frames to be picked up will be displayed.
	 */
	uint64_t slot_next_frame_display;

	/*!
	 * Currently being transferred or waited on.
	 * Not protected by the slot lock as it is only touched by the client thread.
	 */
	struct multi_layer_slot progress;

	//! Scheduled frames for a future timepoint.
	struct multi_layer_slot scheduled;

	/*!
	 * Fully ready to be used.
	 * Not protected by the slot lock as it is only touched by the main render loop thread.
	 */
	struct multi_layer_slot delivered;

	struct u_pacing_app *upa;
};

static inline struct multi_compositor *
multi_compositor(struct xrt_compositor *xc)
{
	return (struct multi_compositor *)xc;
}

/*!
 * Create a multi client wrapper compositor.
 *
 * @ingroup comp_multi
 */
xrt_result_t
multi_compositor_create(struct multi_system_compositor *msc,
                        const struct xrt_session_info *xsi,
                        struct xrt_compositor_native **out_xcn);

/*!
 * Push a event to be delivered to the client.
 *
 * @ingroup comp_multi
 * @private @memberof multi_compositor
 */
void
multi_compositor_push_event(struct multi_compositor *mc, const union xrt_compositor_event *xce);

/*!
 * Deliver any scheduled frames at that is to be display at or after the given @p display_time_ns. Called by the render
 * thread and copies data from multi_compositor::scheduled to multi_compositor::delivered while holding the slot_lock.
 *
 * @ingroup comp_multi
 * @private @memberof multi_compositor
 */
void
multi_compositor_deliver_any_frames(struct multi_compositor *mc, uint64_t display_time_ns);


/*
 *
 * Multi-client-capable system compositor
 *
 */

/*!
 * State of the multi-client system compositor. Use to track the calling of native
 * compositor methods @ref xrt_comp_begin_session and @ref xrt_comp_end_session.
 *
 * It is driven by the number of active app sessions.
 *
 * @ingroup comp_multi
 */
enum multi_system_state
{
	/*!
	 * Invalid state, never used.
	 */
	MULTI_SYSTEM_STATE_INVALID,

	/*!
	 * One of the initial states, the multi-client system compositor will
	 * make sure that its @ref xrt_compositor_native submits one frame.
	 *
	 * The session hasn't been started yet.
	 */
	MULTI_SYSTEM_STATE_INIT_WARM_START,

	/*!
	 * One of the initial state and post stopping state.
	 *
	 * The multi-client system compositor has called @ref xrt_comp_end_session
	 * on its @ref xrt_compositor_native.
	 */
	MULTI_SYSTEM_STATE_STOPPED,

	/*!
	 * The main session is running.
	 *
	 * The multi-client system compositor has called @ref xrt_comp_begin_session
	 * on its @ref xrt_compositor_native.
	 */
	MULTI_SYSTEM_STATE_RUNNING,

	/*!
	 * There are no active sessions and the multi-client system compositor is
	 * instructing the native compositor to draw one or more clear frames.
	 *
	 * The multi-client system compositor has not yet called @ref xrt_comp_begin_session
	 * on its @ref xrt_compositor_native.
	 */
	MULTI_SYSTEM_STATE_STOPPING,
};

/*!
 * The multi-client system compositor multiplexes access to a single native compositor,
 * merging layers from one or more client apps/sessions.
 *
 * @ingroup comp_multi
 * @implements xrt_system_compositor
 */
struct multi_system_compositor
{
	struct xrt_system_compositor base;

	//! Extra functions to handle multi client.
	struct xrt_multi_compositor_control xmcc;

	//! Real native compositor.
	struct xrt_compositor_native *xcn;

	//! App pacer factory.
	struct u_pacing_app_factory *upaf;

	//! Render loop thread.
	struct os_thread_helper oth;

	struct
	{
		/*!
		 * The state of the multi-client system compositor.
		 * This is updated on the multi_system_compositor::oth
		 * thread, aka multi-client system compositor main thread.
		 * It is driven by the active_count field.
		 */
		enum multi_system_state state;

		//! Number of active sessions, protected by oth.
		uint64_t active_count;
	} sessions;

	/*!
	 * This mutex protects the list of client compositor
	 * and the rendering timings on it.
	 */
	struct os_mutex list_and_timing_lock;

	struct
	{
		uint64_t predicted_display_time_ns;
		uint64_t predicted_display_period_ns;
		uint64_t diff_ns;
	} last_timings;

	struct multi_compositor *clients[MULTI_MAX_CLIENTS];
};

/*!
 * Cast helper
 *
 * @ingroup comp_multi
 * @private @memberof multi_system_compositor
 */
static inline struct multi_system_compositor *
multi_system_compositor(struct xrt_system_compositor *xsc)
{
	return (struct multi_system_compositor *)xsc;
}

/*!
 * The client compositor calls this function to update when its session is
 * started or stopped.
 *
 * @ingroup comp_multi
 * @private @memberof multi_system_compositor
 */
void
multi_system_compositor_update_session_status(struct multi_system_compositor *msc, bool active);


#ifdef __cplusplus
}
#endif
