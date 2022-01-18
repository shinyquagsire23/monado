// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared pacing code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup aux_pacing Frame and Render timing/pacing
 *
 * @ingroup aux_util
 * @see @ref frame-pacing.
 */


/*!
 * For marking timepoints on a frame's lifetime, not a async event.
 *
 * @ingroup aux_pacing
 */
enum u_timing_point
{
	U_TIMING_POINT_WAKE_UP, //!< Woke up after sleeping in wait frame.
	U_TIMING_POINT_BEGIN,   //!< Began CPU side work for GPU.
	U_TIMING_POINT_SUBMIT,  //!< Submitted work to the GPU.
};


/*
 *
 * Compositor pacing helper.
 *
 */

/*!
 * Compositor pacing helper interface.
 *
 * This is used for the compositor's own frame timing/pacing. It is not responsible for getting the timing data from the
 * graphics API, etc: instead it consumes timing data from the graphics API (if available) and from "markers" in the
 * compositor's CPU code, and produces predictions that are used to guide the compositor.
 *
 * Pacing of the underlying app/client is handled by @ref u_pacing_app
 *
 * @ingroup aux_pacing
 */
struct u_pacing_compositor
{
	/*!
	 * Predict the next frame.
	 *
	 * @param[in] upc                                The compositor pacing helper.
	 * @param[out] out_frame_id                      Id used to refer to this frame again.
	 * @param[out] out_wake_up_time_ns               When should the compositor wake up.
	 * @param[out] out_desired_present_time_ns       The GPU should start scanning out at this time.
	 * @param[out] out_present_slop_ns               Any looseness to the desired present timing.
	 * @param[out] out_predicted_display_time_ns     At what time have we predicted that pixels turns to photons.
	 * @param[out] out_predicted_display_period_ns   Display period that we are running on.
	 * @param[out] out_min_display_period_ns         The fastest theoretical display period.
	 *
	 * @see @ref frame-pacing.
	 */
	void (*predict)(struct u_pacing_compositor *upc,
	                int64_t *out_frame_id,
	                uint64_t *out_wake_up_time_ns,
	                uint64_t *out_desired_present_time_ns,
	                uint64_t *out_present_slop_ns,
	                uint64_t *out_predicted_display_time_ns,
	                uint64_t *out_predicted_display_period_ns,
	                uint64_t *out_min_display_period_ns);

	/*!
	 * Mark a point on the frame's lifetime.
	 *
	 * This is usually provided "when it happens" because the points to mark are steps in the CPU workload of the
	 * compositor.
	 *
	 * @param[in] upc      The compositor pacing helper.
	 * @param[in] point    The point to record for a frame.
	 * @param[in] frame_id The frame ID to record for.
	 * @param[in] when_ns  The timestamp of the event.
	 *
	 * @see @ref frame-pacing.
	 */
	void (*mark_point)(struct u_pacing_compositor *upc,
	                   enum u_timing_point point,
	                   int64_t frame_id,
	                   uint64_t when_ns);

	/*!
	 * Provide frame timing information about a delivered frame.
	 *
	 * This is usually provided after-the-fact by the display system. These arguments currently
	 * matches 1-to-1 what VK_GOOGLE_display_timing provides, see
	 * https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkPastPresentationTimingGOOGLE.html
	 *
	 * Depend on when the information is delivered this can be called at any
	 * point of the following frames.
	 *
	 * @param[in] upc                      The compositor pacing helper.
	 * @param[in] frame_id                 The frame ID to record for.
	 * @param[in] desired_present_time_ns  The time that we indicated the GPU should start scanning out at, or zero
	 *                                     if we didn't provide such a time.
	 * @param[in] actual_present_time_ns   The time that the GPU actually started scanning out.
	 * @param[in] earliest_present_time_ns The earliest the GPU could have presented - might be before @p
	 *                                     actual_present_time_ns if a @p desired_present_time_ns was passed.
	 * @param[in] present_margin_ns        How "early" present happened compared to when it needed to happen in
	 *                                     order to hit @p earliestPresentTime.
	 *
	 * @see @ref frame-pacing.
	 */
	void (*info)(struct u_pacing_compositor *upc,
	             int64_t frame_id,
	             uint64_t desired_present_time_ns,
	             uint64_t actual_present_time_ns,
	             uint64_t earliest_present_time_ns,
	             uint64_t present_margin_ns);

	/*!
	 * Destroy this u_pacing_compositor.
	 */
	void (*destroy)(struct u_pacing_compositor *upc);
};

/*!
 * @copydoc u_pacing_compositor::predict
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_pacing_compositor
 * @ingroup aux_pacing
 */
static inline void
u_pc_predict(struct u_pacing_compositor *upc,
             int64_t *out_frame_id,
             uint64_t *out_wake_up_time_ns,
             uint64_t *out_desired_present_time_ns,
             uint64_t *out_present_slop_ns,
             uint64_t *out_predicted_display_time_ns,
             uint64_t *out_predicted_display_period_ns,
             uint64_t *out_min_display_period_ns)
{
	upc->predict(upc,                             //
	             out_frame_id,                    //
	             out_wake_up_time_ns,             //
	             out_desired_present_time_ns,     //
	             out_present_slop_ns,             //
	             out_predicted_display_time_ns,   //
	             out_predicted_display_period_ns, //
	             out_min_display_period_ns);      //
}

/*!
 * @copydoc u_pacing_compositor::mark_point
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_pacing_compositor
 * @ingroup aux_pacing
 */
static inline void
u_pc_mark_point(struct u_pacing_compositor *upc, enum u_timing_point point, int64_t frame_id, uint64_t when_ns)
{
	upc->mark_point(upc, point, frame_id, when_ns);
}

/*!
 * @copydoc u_pacing_compositor::info
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_pacing_compositor
 * @ingroup aux_pacing
 */
static inline void
u_pc_info(struct u_pacing_compositor *upc,
          int64_t frame_id,
          uint64_t desired_present_time_ns,
          uint64_t actual_present_time_ns,
          uint64_t earliest_present_time_ns,
          uint64_t present_margin_ns)
{
	upc->info(upc, frame_id, desired_present_time_ns, actual_present_time_ns, earliest_present_time_ns,
	          present_margin_ns);
}

/*!
 * @copydoc u_pacing_compositor::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * @p upc_ptr to null if freed.
 *
 * @public @memberof u_pacing_compositor
 * @ingroup aux_pacing
 */
static inline void
u_pc_destroy(struct u_pacing_compositor **upc_ptr)
{
	struct u_pacing_compositor *upc = *upc_ptr;
	if (upc == NULL) {
		return;
	}

	upc->destroy(upc);
	*upc_ptr = NULL;
}


/*
 *
 * Render timing helper.
 *
 */

/*!
 * This application pacing helper is designed to schedule the rendering time of
 * clients that submit frames to a compositor, which runs its own render loop
 * that picks latest completed frames for that client.
 *
 * It manages the frame pacing of an app/client, rather than the compositor itself. The frame pacing of the compositor
 * is handled by @ref u_pacing_compositor
 *
 * @ingroup aux_pacing
 */
struct u_pacing_app
{
	/*!
	 * Predict when the client's next rendered frame will be displayed; when the
	 * client should be woken up from sleeping; and its display period.
	 *
	 * This is called from `xrWaitFrame`, but it does not do any waiting, the caller
	 * should wait till `out_wake_up_time`.
	 *
	 * @param      upa                          Render timing helper.
	 * @param[out] out_frame_id                 Frame ID of this predicted frame.
	 * @param[out] out_wake_up_time             When the client should be woken up.
	 * @param[out] out_predicted_display_time   Predicted display time.
	 * @param[out] out_predicted_display_period Predicted display period.
	 */
	void (*predict)(struct u_pacing_app *upa,
	                int64_t *out_frame_id,
	                uint64_t *out_wake_up_time,
	                uint64_t *out_predicted_display_time,
	                uint64_t *out_predicted_display_period);

	/*!
	 * Mark a point on the frame's lifetime.
	 *
	 * @param      upa     Render timing helper.
	 * @param[in] frame_id The frame ID to record for.
	 * @see @ref frame-pacing.
	 */
	void (*mark_point)(struct u_pacing_app *upa, int64_t frame_id, enum u_timing_point point, uint64_t when_ns);

	/*!
	 * When a frame has been discarded.
	 */
	void (*mark_discarded)(struct u_pacing_app *upa, int64_t frame_id);

	/*!
	 * A frame has been delivered from the client, see `xrEndFrame`. The GPU might
	 * still be rendering the work.
	 */
	void (*mark_delivered)(struct u_pacing_app *upa, int64_t frame_id);

	/*!
	 * Add a new sample point from the main render loop.
	 *
	 * This is called in the main renderer loop that tightly submits frames to the
	 * real compositor for displaying. This is only used to inform the render helper
	 * when the frame will be shown, not any timing information about the client.
	 *
	 * When this is called doesn't matter that much, as the render timing will need
	 * to be able to predict one or more frames into the future anyways. But
	 * preferably as soon as the main loop wakes up from wait frame.
	 *
	 * @param upa                         Self pointer
	 * @param predicted_display_time_ns   Predicted display time for this sample.
	 * @param predicted_display_period_ns Predicted display period for this sample.
	 * @param extra_ns                    Time between display and when this sample
	 *                                    was created, that is when the main loop
	 *                                    was woken up by the main compositor.
	 */
	void (*info)(struct u_pacing_app *upa,
	             uint64_t predicted_display_time_ns,
	             uint64_t predicted_display_period_ns,
	             uint64_t extra_ns);

	/*!
	 * Destroy this u_pacing_app.
	 */
	void (*destroy)(struct u_pacing_app *upa);
};

/*!
 * @copydoc u_pacing_app::predict
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_pacing_app
 * @ingroup aux_pacing
 */
static inline void
u_pa_predict(struct u_pacing_app *upa,
             int64_t *out_frame_id,
             uint64_t *out_wake_up_time,
             uint64_t *out_predicted_display_time,
             uint64_t *out_predicted_display_period)
{
	upa->predict(upa, out_frame_id, out_wake_up_time, out_predicted_display_time, out_predicted_display_period);
}

/*!
 * @copydoc u_pacing_app::mark_point
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_pacing_app
 * @ingroup aux_pacing
 */
static inline void
u_pa_mark_point(struct u_pacing_app *upa, int64_t frame_id, enum u_timing_point point, uint64_t when_ns)
{
	upa->mark_point(upa, frame_id, point, when_ns);
}

/*!
 * @copydoc u_pacing_app::mark_discarded
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_pacing_app
 * @ingroup aux_pacing
 */
static inline void
u_pa_mark_discarded(struct u_pacing_app *upa, int64_t frame_id)
{
	upa->mark_discarded(upa, frame_id);
}

/*!
 * @copydoc u_pacing_app::mark_delivered
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_pacing_app
 * @ingroup aux_pacing
 */
static inline void
u_pa_mark_delivered(struct u_pacing_app *upa, int64_t frame_id)
{
	upa->mark_delivered(upa, frame_id);
}

/*!
 * @copydoc u_pacing_app::info
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_pacing_app
 * @ingroup aux_pacing
 */
static inline void
u_pa_info(struct u_pacing_app *upa,
          uint64_t predicted_display_time_ns,
          uint64_t predicted_display_period_ns,
          uint64_t extra_ns)
{
	upa->info(upa, predicted_display_time_ns, predicted_display_period_ns, extra_ns);
}

/*!
 * @copydoc u_pacing_app::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * upa_ptr to null if freed.
 *
 * @public @memberof u_pacing_compositor
 * @ingroup aux_pacing
 */
static inline void
u_pa_destroy(struct u_pacing_app **upa_ptr)
{
	struct u_pacing_app *upa = *upa_ptr;
	if (upa == NULL) {
		return;
	}

	upa->destroy(upa);
	*upa_ptr = NULL;
}

/*
 *
 * Configuration struct
 *
 */
/*!
 * Configuration for the "display-timing-aware" implementation of @ref u_pacing_compositor
 *
 * @see u_pc_display_timing_create
 */
struct u_pc_display_timing_config
{
	//! How long after "present" is the image actually displayed
	uint64_t present_offset_ns;
	//! Extra margin that is added to app time, between end of draw and present
	uint64_t margin_ns;
	/*!
	 * @name Frame-Relative Values
	 * All these values are in "percentage points of the nominal frame period" so they can work across
	 * devices of varying refresh rate/display interval.
	 * @{
	 */
	//! The initial estimate of how much time the app needs
	uint32_t app_time_fraction;
	//! The maximum time we allow to the app
	uint32_t app_time_max_fraction;
	//! When missing a frame, back off in these increments
	uint32_t adjust_missed_fraction;
	//! When not missing frames but adjusting app time at these increments
	uint32_t adjust_non_miss_fraction;
	/*!
	 * @}
	 */
};

/*!
 * Default configuration values for display-timing-aware compositor pacing.
 *
 * @see u_pc_display_timing_config, u_pc_display_timing_create
 */
extern const struct u_pc_display_timing_config U_PC_DISPLAY_TIMING_CONFIG_DEFAULT;

/*
 *
 * Implementations.
 *
 */

/*!
 * Creates a new composition pacing helper that uses real display timing information.
 *
 * Meant to be used with `VK_GOOGLE_display_timing`.
 *
 * @ingroup aux_pacing
 * @see u_pacing_compositor
 */
xrt_result_t
u_pc_display_timing_create(uint64_t estimated_frame_period_ns,
                           const struct u_pc_display_timing_config *config,
                           struct u_pacing_compositor **out_upc);

/*!
 * Creates a new composition pacing helper that does not depend on display timing information.
 *
 * When you cannot get display timing information, use this.
 *
 * @ingroup aux_pacing
 * @see u_pacing_compositor
 */
xrt_result_t
u_pc_fake_create(uint64_t estimated_frame_period_ns, struct u_pacing_compositor **out_upc);

/*!
 * Creates a new application pacing helper.
 *
 * @ingroup aux_pacing
 * @see u_pacing_app
 */
xrt_result_t
u_pa_create(struct u_pacing_app **out_upa);


#ifdef __cplusplus
}
#endif
