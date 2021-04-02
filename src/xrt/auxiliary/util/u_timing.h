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


/*
 *
 * Frame timing helper.
 *
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


/*
 *
 * Helper functions.
 *
 */

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


#ifdef __cplusplus
}
#endif
