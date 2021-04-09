// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared timing code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup aux_timing Frame and Render timing
 *
 * @ingroup aux_util
 * @see @ref frame-timing.
 */


/*!
 * For marking timepoints on a frame's lifetime, not a async event.
 *
 * @ingroup aux_timing
 */
enum u_timing_point
{
	U_TIMING_POINT_WAKE_UP, //!< Woke up after sleeping in wait frame.
	U_TIMING_POINT_BEGIN,   //!< Began CPU side work for GPU.
	U_TIMING_POINT_SUBMIT,  //!< Submitted work to the GPU.
};


/*
 *
 * Frame timing helper.
 *
 */

/*!
 * Frame timing helper struct, used for the compositors own frame timing.
 *
 * @ingroup aux_timing
 */
struct u_frame_timing
{
	/*!
	 * Predict the next frame.
	 *
	 * @param[in] uft                                The frame timing struct.
	 * @param[out] out_frame_id                      Id used to refer to this frame again.
	 * @param[out] out_wake_up_time_ns               When should the compositor wake up.
	 * @param[out] out_desired_present_time_ns       The GPU should start scanning out at this time.
	 * @param[out] out_present_slop_ns               Any looseness to the desired present timing.
	 * @param[out] out_predicted_display_time_ns     At what time have we predicted that pixels turns to photons.
	 * @param[out] out_predicted_display_period_ns   Display period that we are running on.
	 * @param[out] out_min_display_period_ns         The fastest theoretical display period.
	 *
	 * @see @ref frame-timing.
	 */
	void (*predict)(struct u_frame_timing *uft,
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
	 * @see @ref frame-timing.
	 */
	void (*mark_point)(struct u_frame_timing *uft, enum u_timing_point point, int64_t frame_id, uint64_t when_ns);

	/*!
	 * Provide frame timing information about a delivered frame, this is
	 * usually provided by the display system. These arguments currently
	 * matches 1-to-1 what VK_GOOGLE_display_timing provides.
	 *
	 * Depend on when the information is delivered this can be called at any
	 * point of the following frames.
	 *
	 * @see @ref frame-timing.
	 */
	void (*info)(struct u_frame_timing *uft,
	             int64_t frame_id,
	             uint64_t desired_present_time_ns,
	             uint64_t actual_present_time_ns,
	             uint64_t earliest_present_time_ns,
	             uint64_t present_margin_ns);

	/*!
	 * Destroy this u_frame_timing.
	 */
	void (*destroy)(struct u_frame_timing *uft);
};

/*!
 * @copydoc u_frame_timing::predict
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_frame_timing
 * @ingroup aux_timing
 */
static inline void
u_ft_predict(struct u_frame_timing *uft,
             int64_t *out_frame_id,
             uint64_t *out_wake_up_time_ns,
             uint64_t *out_desired_present_time_ns,
             uint64_t *out_present_slop_ns,
             uint64_t *out_predicted_display_time_ns,
             uint64_t *out_predicted_display_period_ns,
             uint64_t *out_min_display_period_ns)
{
	uft->predict(uft,                             //
	             out_frame_id,                    //
	             out_wake_up_time_ns,             //
	             out_desired_present_time_ns,     //
	             out_present_slop_ns,             //
	             out_predicted_display_time_ns,   //
	             out_predicted_display_period_ns, //
	             out_min_display_period_ns);      //
}

/*!
 * @copydoc u_frame_timing::mark_point
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_frame_timing
 * @ingroup aux_timing
 */
static inline void
u_ft_mark_point(struct u_frame_timing *uft, enum u_timing_point point, int64_t frame_id, uint64_t when_ns)
{
	uft->mark_point(uft, point, frame_id, when_ns);
}

/*!
 * @copydoc u_frame_timing::info
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_frame_timing
 * @ingroup aux_timing
 */
static inline void
u_ft_info(struct u_frame_timing *uft,
          int64_t frame_id,
          uint64_t desired_present_time_ns,
          uint64_t actual_present_time_ns,
          uint64_t earliest_present_time_ns,
          uint64_t present_margin_ns)
{
	uft->info(uft, frame_id, desired_present_time_ns, actual_present_time_ns, earliest_present_time_ns,
	          present_margin_ns);
}

/*!
 * @copydoc u_frame_timing::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * uft_ptr to null if freed.
 *
 * @public @memberof u_frame_timing
 * @ingroup aux_timing
 */
static inline void
u_ft_destroy(struct u_frame_timing **uft_ptr)
{
	struct u_frame_timing *uft = *uft_ptr;
	if (uft == NULL) {
		return;
	}

	uft->destroy(uft);
	*uft_ptr = NULL;
}


/*
 *
 * Render timing helper.
 *
 */

/*!
 * This render timing helper is designed to schedule the rendering time of
 * clients that submit frames to a compositor, which runs its own render loop
 * that picks latest completed frames for that client.
 *
 * @ingroup aux_timing
 */
struct u_render_timing
{
	/*!
	 * Predict when the client's next: rendered frame will be display; when the
	 * client should be woken up from sleeping; and its display period.
	 *
	 * This is called from `xrWaitFrame`, but it does not do any waiting, the caller
	 * should wait till `out_wake_up_time`.
	 *
	 * @param      urt                          Render timing helper.
	 * @param[out] out_frame_id                 Frame ID of this predicted frame.
	 * @param[out] out_wake_up_time             When the client should be woken up.
	 * @param[out] out_predicted_display_time   Predicted display time.
	 * @param[out] out_predicted_display_period Predicted display period.
	 */
	void (*predict)(struct u_render_timing *urt,
	                int64_t *out_frame_id,
	                uint64_t *out_wake_up_time,
	                uint64_t *out_predicted_display_time,
	                uint64_t *out_predicted_display_period);

	/*!
	 * Mark a point on the frame's lifetime.
	 *
	 * @see @ref frame-timing.
	 */
	void (*mark_point)(struct u_render_timing *urt, int64_t frame_id, enum u_timing_point point, uint64_t when_ns);

	/*!
	 * When a frame has been discarded.
	 */
	void (*mark_discarded)(struct u_render_timing *urt, int64_t frame_id);

	/*!
	 * A frame has been delivered from the client, see `xrEndFrame`. The GPU might
	 * still be rendering the work.
	 */
	void (*mark_delivered)(struct u_render_timing *urt, int64_t frame_id);

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
	 * @param urt                         Self pointer
	 * @param predicted_display_time_ns   Predicted display time for this sample.
	 * @param predicted_display_period_ns Predicted display period for this sample.
	 * @param extra_ns                    Time between display and when this sample
	 *                                    was created, that is when the main loop
	 *                                    was woken up by the main compositor.
	 */
	void (*info)(struct u_render_timing *urt,
	             uint64_t predicted_display_time_ns,
	             uint64_t predicted_display_period_ns,
	             uint64_t extra_ns);

	/*!
	 * Destroy this u_render_timing.
	 */
	void (*destroy)(struct u_render_timing *urt);
};

/*!
 * @copydoc u_render_timing::predict
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_render_timing
 * @ingroup aux_timing
 */
static inline void
u_rt_predict(struct u_render_timing *urt,
             int64_t *out_frame_id,
             uint64_t *out_wake_up_time,
             uint64_t *out_predicted_display_time,
             uint64_t *out_predicted_display_period)
{
	urt->predict(urt, out_frame_id, out_wake_up_time, out_predicted_display_time, out_predicted_display_period);
}

/*!
 * @copydoc u_render_timing::mark_point
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_render_timing
 * @ingroup aux_timing
 */
static inline void
u_rt_mark_point(struct u_render_timing *urt, int64_t frame_id, enum u_timing_point point, uint64_t when_ns)
{
	urt->mark_point(urt, frame_id, point, when_ns);
}

/*!
 * @copydoc u_render_timing::mark_discarded
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_render_timing
 * @ingroup aux_timing
 */
static inline void
u_rt_mark_discarded(struct u_render_timing *urt, int64_t frame_id)
{
	urt->mark_discarded(urt, frame_id);
}

/*!
 * @copydoc u_render_timing::mark_delivered
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_render_timing
 * @ingroup aux_timing
 */
static inline void
u_rt_mark_delivered(struct u_render_timing *urt, int64_t frame_id)
{
	urt->mark_delivered(urt, frame_id);
}

/*!
 * @copydoc u_render_timing::info
 *
 * Helper for calling through the function pointer.
 *
 * @public @memberof u_render_timing
 * @ingroup aux_timing
 */
static inline void
u_rt_info(struct u_render_timing *urt,
          uint64_t predicted_display_time_ns,
          uint64_t predicted_display_period_ns,
          uint64_t extra_ns)
{
	urt->info(urt, predicted_display_time_ns, predicted_display_period_ns, extra_ns);
}

/*!
 * @copydoc u_render_timing::destroy
 *
 * Helper for calling through the function pointer: does a null check and sets
 * urt_ptr to null if freed.
 *
 * @public @memberof u_frame_timing
 * @ingroup aux_timing
 */
static inline void
u_rt_destroy(struct u_render_timing **urt_ptr)
{
	struct u_render_timing *urt = *urt_ptr;
	if (urt == NULL) {
		return;
	}

	urt->destroy(urt);
	*urt_ptr = NULL;
}


/*
 *
 * Implementations.
 *
 */

/*!
 * Meant to be used with VK_GOOGLE_display_timing.
 *
 * @ingroup aux_timing
 */
xrt_result_t
u_ft_display_timing_create(uint64_t estimated_frame_period_ns, struct u_frame_timing **out_uft);

/*!
 * When you can not get display timing information use this.
 *
 * @ingroup aux_timing
 */
xrt_result_t
u_ft_fake_create(uint64_t estimated_frame_period_ns, struct u_frame_timing **out_uft);

/*!
 * Creates a new render timing.
 *
 * @ingroup aux_timing
 */
xrt_result_t
u_rt_create(struct u_render_timing **out_urt);


#ifdef __cplusplus
}
#endif
