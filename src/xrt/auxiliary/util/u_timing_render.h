// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared frame timing code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "os/os_time.h"
#include "util/u_timing.h"


#ifdef __cplusplus
extern "C" {
#endif


enum u_rt_state
{
	U_RT_READY,
	U_RT_WAIT_LEFT,
	U_RT_PREDICTED,
	U_RT_BEGUN,
};

struct u_rt_frame
{
	//! When we predicted this frame to be shown.
	uint64_t predicted_display_time_ns;
	//! When the client should have delivered the frame.
	uint64_t predicted_delivery_time_ns;

	struct
	{
		uint64_t predicted_ns;
		uint64_t wait_woke_ns;
		uint64_t begin_ns;
		uint64_t delivered_ns;
	} when; //!< When something happened.

	int64_t frame_id;
	enum u_rt_state state;
};

/*!
 * This render timing helper is designed to schedule the rendering time of
 * clients that submit frames to a compositor, which runs its own render loop
 * that picks latest completed frames for that client.
 */
struct u_rt_helper
{
	struct u_rt_frame frames[2];
	uint32_t current_frame;
	uint32_t next_frame;

	int64_t frame_counter;

	struct
	{
		//! App time between wait returning and begin being called.
		uint64_t cpu_time_ns;
		//! Time between begin and frame rendering completeing.
		uint64_t draw_time_ns;
		//! Exrta time between end of draw time and when the compositor wakes up.
		uint64_t margin_ns;
	} app; //!< App statistics.

	struct
	{
		//! The last display time that the thing driving this helper got.
		uint64_t predicted_display_time_ns;
		//! The last display period the hardware is running at.
		uint64_t predicted_display_period_ns;
		//! The extra time needed by the thing driving this helper.
		uint64_t extra_ns;
	} last_input;

	uint64_t last_returned_ns;
};

void
u_rt_helper_init(struct u_rt_helper *urth);

/*!
 * This function gets the client part of the render timing helper ready to be
 * used. If you use init you will also clear all of the timing information.
 *
 * Call this when resetting a client.
 */
void
u_rt_helper_client_clear(struct u_rt_helper *urth);

/*!
 * Predict when the client's next: rendered frame will be display; when the
 * client should be woken up from sleeping; and its display period.
 *
 * This is called from `xrWaitFrame`, but it does not do any waiting, the caller
 * should wait till `out_wake_up_time`.
 *
 * @param      urth                         Render timing helper.
 * @param[out] out_frame_id                 Frame ID of this predicted frame.
 * @param[out] out_wake_up_time             When the client should be woken up.
 * @param[out] out_predicted_display_time   Predicted display time.
 * @param[out] out_predicted_display_period Predicted display period.
 */
void
u_rt_helper_predict(struct u_rt_helper *urth,
                    int64_t *out_frame_id,
                    uint64_t *out_wake_up_time,
                    uint64_t *out_predicted_display_time,
                    uint64_t *out_predicted_display_period);

/*!
 * Mark a point on the frame's lifetime.
 *
 * @see @ref frame-timing.
 */
void
u_rt_helper_mark(struct u_rt_helper *urth, int64_t frame_id, enum u_timing_point point, uint64_t when_ns);

/*!
 * When a frame has been discarded.
 */
void
u_rt_helper_mark_discarded(struct u_rt_helper *urth, int64_t frame_id);

/*!
 * A frame has been delivered from the client, see `xrEndFrame`. The GPU might
 * still be rendering the work.
 */
void
u_rt_helper_mark_delivered(struct u_rt_helper *urth, int64_t frame_id);

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
 * @param urth                        Self pointer
 * @param predicted_display_time_ns   Predicted display time for this sample.
 * @param predicted_display_period_ns Predicted display period for this sample.
 * @param extra_ns                    Time between display and when this sample
 *                                    was created, that is when the main loop
 *                                    was woken up by the main compositor.
 */
void
u_rt_helper_new_sample(struct u_rt_helper *urth,
                       uint64_t predicted_display_time_ns,
                       uint64_t predicted_display_period_ns,
                       uint64_t extra_ns);


#ifdef __cplusplus
}
#endif
