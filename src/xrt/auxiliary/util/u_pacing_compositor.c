// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared frame timing code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "os/os_time.h"

#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_pacing.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

DEBUG_GET_ONCE_LOG_OPTION(log_level, "U_TIMING_FRAME_LOG", U_LOGGING_WARN)

#define FT_LOG_T(...) U_LOG_IFL_T(debug_get_log_option_log_level(), __VA_ARGS__)
#define FT_LOG_D(...) U_LOG_IFL_D(debug_get_log_option_log_level(), __VA_ARGS__)
#define FT_LOG_I(...) U_LOG_IFL_I(debug_get_log_option_log_level(), __VA_ARGS__)
#define FT_LOG_W(...) U_LOG_IFL_W(debug_get_log_option_log_level(), __VA_ARGS__)
#define FT_LOG_E(...) U_LOG_IFL_E(debug_get_log_option_log_level(), __VA_ARGS__)

#define NUM_FRAMES 16


/*
 *
 * Display timing code.
 *
 */

enum frame_state
{
	STATE_SKIPPED = -1,
	STATE_CLEARED = 0,
	STATE_PREDICTED = 1,
	STATE_WOKE = 2,
	STATE_BEGAN = 3,
	STATE_SUBMITTED = 4,
	STATE_INFO = 5,
};

struct frame
{
	//! An arbitrary id that identifies this frame. Set in `create_frame`.
	int64_t frame_id;

	//! When this frame was last used for a prediction. Set in `predict_next_frame`.
	uint64_t when_predict_ns;

	//! When should the compositor wake the app up. Set in `predict_next_frame`.
	uint64_t wake_up_time_ns;

	//! When the compositor last woke up the app after wait_frame. Set in `dt_mark_point` with
	//! `U_TIMING_POINT_WAKE_UP`.
	uint64_t when_woke_ns;

	//! When the compositor started rendering a frame
	uint64_t when_began_ns;

	//! When the compositor finished rendering a frame
	uint64_t when_submitted_ns;

	//! When new frame timing info was last added.
	uint64_t when_infoed_ns;

	//! How much time we currently expect the app to take rendering a frame. Updated in `predict_next_frame`
	uint64_t current_app_time_ns;

	uint64_t expected_done_time_ns;     //!< When we expect the compositor to be done with its frame.
	uint64_t desired_present_time_ns;   //!< The GPU should start scanning out at this time.
	uint64_t predicted_display_time_ns; //!< At what time have we predicted that pixels turns to photons.
	uint64_t present_margin_ns;
	uint64_t actual_present_time_ns;
	uint64_t earliest_present_time_ns;

	enum frame_state state;
};

struct display_timing
{
	struct u_pacing_compositor base;

	/*!
	 * Very often the present time that we get from the system is only when
	 * the display engine starts scanning out from the buffers we provided,
	 * and not when the pixels turned into photons that the user sees.
	 */
	uint64_t present_offset_ns;

	/*!
	 * Frame period of the device.
	 */
	uint64_t frame_period_ns;

	/*!
	 * The amount of time that the application needs to render frame.
	 */
	uint64_t app_time_ns;

	/*!
	 * The amount of time that the application needs to render frame.
	 */
	uint64_t padding_time_ns;

	/*!
	 * Used to generate frame IDs.
	 */
	int64_t next_frame_id;

	/*!
	 * The maximum amount we give to the 'app'.
	 */
	uint64_t app_time_max_ns;

	/*!
	 * If we missed a frame, back off this much.
	 */
	uint64_t adjust_missed_ns;

	/*!
	 * Adjustment of time if we didn't miss the frame,
	 * also used as range to stay around timing target.
	 */
	uint64_t adjust_non_miss_ns;

	/*!
	 * Extra time between end of draw time and when the present happens.
	 */
	uint64_t margin_ns;

	/*!
	 * Frame store.
	 */
	struct frame frames[NUM_FRAMES];
};


/*
 *
 * Helper functions.
 *
 */

static inline struct display_timing *
display_timing(struct u_pacing_compositor *upc)
{
	return (struct display_timing *)upc;
}

static double
ns_to_ms(int64_t t)
{
	return (double)(t / 1000) / 1000.0;
}

static uint64_t
get_percent_of_time(uint64_t time_ns, uint32_t fraction_percent)
{
	double fraction = (double)fraction_percent / 100.0;
	return time_s_to_ns(time_ns_to_s(time_ns) * fraction);
}

static uint64_t
calc_total_app_time(struct display_timing *dt)
{
	return dt->app_time_ns + dt->margin_ns;
}

static uint64_t
calc_display_time_from_present_time(struct display_timing *dt, uint64_t desired_present_time_ns)
{
	return desired_present_time_ns + dt->present_offset_ns;
}

static inline bool
is_within_of_each_other(uint64_t l, uint64_t r, uint64_t range)
{
	int64_t t = (int64_t)l - (int64_t)r;
	return (-(int64_t)range < t) && (t < (int64_t)range);
}

static inline bool
is_within_half_ms(uint64_t l, uint64_t r)
{
	return is_within_of_each_other(l, r, U_TIME_HALF_MS_IN_NS);
}

/*!
 * Gets a frame data structure based on the @p frame_id.
 *
 * Note that this is done modulo the number of frame data structs we hold: the data in the frame you receive may not
 * match the @p frame_id you passed!
 *
 * @see create_frame to create a frame id and (partially) initialize the frame data structure, @ref do_clean_slate_frame
 * for a more complete initialization
 */
static struct frame *
get_frame(struct display_timing *dt, int64_t frame_id)
{
	assert(frame_id >= 0);
	assert((uint64_t)frame_id <= (uint64_t)SIZE_MAX);

	size_t index = (size_t)(frame_id % NUM_FRAMES);

	return &dt->frames[index];
}

/*!
 * Assign the next available frame ID, initialize the corresponding frame data with the ID and @p state, and return a
 * pointer to that frame data.
 *
 * Fields other than frame::frame_id and frame::state are not modified, so may have old data in them. This may be a
 * feature rather than a bug.
 */
static struct frame *
create_frame(struct display_timing *dt, enum frame_state state)
{
	int64_t frame_id = dt->next_frame_id++;
	struct frame *f = get_frame(dt, frame_id);

	f->frame_id = frame_id;
	f->state = state;

	return f;
}

/*!
 * Gets the most recent frame data whose state is greater than or equal to @p state, if any
 *
 * @return a frame pointer, or null if no frames have at least @p state
 */
static struct frame *
get_latest_frame_with_state_at_least(struct display_timing *dt, enum frame_state state)
{
	uint64_t start_from = dt->next_frame_id;
	uint64_t count = 1;

	while (start_from >= count && count < NUM_FRAMES) {
		struct frame *f = get_frame(dt, start_from - count++);
		if (f->state >= state) {
			return f;
		}
	}

	return NULL;
}

/*!
 * "Create" a frame ID in state @ref STATE_PREDICTED (by calling @ref create_frame), and additionally initialize
 * frame::desired_present_time_ns (with a crude estimate) and frame::when_predict_ns.
 */
static struct frame *
do_clean_slate_frame(struct display_timing *dt)
{
	struct frame *f = create_frame(dt, STATE_PREDICTED);
	uint64_t now_ns = os_monotonic_get_ns();

	// Wild shot in the dark.
	uint64_t the_time_ns = os_monotonic_get_ns() + dt->frame_period_ns * 10;
	f->when_predict_ns = now_ns;
	f->desired_present_time_ns = the_time_ns;

	return f;
}

/*!
 * Find the next possible present time for rendering that has not yet occurred, and create a frame/frame id with that
 * prediction in it.
 */
static struct frame *
walk_forward_through_frames(struct display_timing *dt, uint64_t last_present_time_ns)
{
	uint64_t now_ns = os_monotonic_get_ns();
	// This is the earliest possible time we could present, assuming rendering still must take place.
	uint64_t from_time_ns = now_ns + calc_total_app_time(dt);
	uint64_t desired_present_time_ns = last_present_time_ns + dt->frame_period_ns;

	while (desired_present_time_ns <= from_time_ns) {
		FT_LOG_D(
		    "Skipped!"                                         //
		    "\n\tfrom_time_ns:            %" PRIu64            //
		    "\n\tdesired_present_time_ns: %" PRIu64            //
		    "\n\tdiff_ms: %.2f",                               //
		    from_time_ns,                                      //
		    desired_present_time_ns,                           //
		    ns_to_ms(from_time_ns - desired_present_time_ns)); //

		// Try next frame period.
		desired_present_time_ns += dt->frame_period_ns;
	}

	struct frame *f = create_frame(dt, STATE_PREDICTED);
	f->when_predict_ns = now_ns;
	f->desired_present_time_ns = desired_present_time_ns;

	return f;
}

static struct frame *
predict_next_frame(struct display_timing *dt)
{
	struct frame *f = NULL;
	// Last earliest display time, can be zero.
	struct frame *last_predicted = get_latest_frame_with_state_at_least(dt, STATE_PREDICTED);
	struct frame *last_completed = get_latest_frame_with_state_at_least(dt, STATE_INFO);
	if (last_predicted == NULL && last_completed == NULL) {
		f = do_clean_slate_frame(dt);
	} else if (last_completed == last_predicted) {
		// Very high propability that we missed a frame.
		f = walk_forward_through_frames(dt, last_completed->earliest_present_time_ns);
	} else if (last_completed != NULL) {
		assert(last_predicted != NULL);
		assert(last_predicted->frame_id > last_completed->frame_id);

		int64_t diff_id = last_predicted->frame_id - last_completed->frame_id;
		int64_t diff_ns = last_completed->desired_present_time_ns - last_completed->earliest_present_time_ns;
		uint64_t adjusted_last_present_time_ns =
		    last_completed->earliest_present_time_ns + diff_id * dt->frame_period_ns;

		if (diff_ns > U_TIME_1MS_IN_NS) {
			FT_LOG_D("Large diff!");
		}
		if (diff_id > 1) {
			FT_LOG_D(
			    "diff_id > 1\n"
			    "\tdiff_id:                       %" PRIi64
			    "\n"
			    "\tadjusted_last_present_time_ns: %" PRIu64,
			    diff_id, adjusted_last_present_time_ns);
		}

		if (diff_id > 1) {
			diff_id = 1;
		}

		f = walk_forward_through_frames(dt, adjusted_last_present_time_ns);
	} else {
		assert(last_predicted != NULL);

		f = walk_forward_through_frames(dt, last_predicted->predicted_display_time_ns);
	}

	f->predicted_display_time_ns = calc_display_time_from_present_time(dt, f->desired_present_time_ns);
	f->wake_up_time_ns = f->desired_present_time_ns - calc_total_app_time(dt);
	f->current_app_time_ns = dt->app_time_ns;

	return f;
}

static void
adjust_app_time(struct display_timing *dt, struct frame *f)
{
	uint64_t app_time_ns = dt->app_time_ns;

	if (f->actual_present_time_ns > f->desired_present_time_ns &&
	    !is_within_half_ms(f->actual_present_time_ns, f->desired_present_time_ns)) {
		double missed_ms = ns_to_ms(f->actual_present_time_ns - f->desired_present_time_ns);
		FT_LOG_W("Frame %" PRIu64 " missed by %.2f!", f->frame_id, missed_ms);

		app_time_ns += dt->adjust_missed_ns;
		if (app_time_ns > dt->app_time_max_ns) {
			app_time_ns = dt->app_time_max_ns;
		}

		dt->app_time_ns = app_time_ns;
		return;
	}

	// We want the GPU work to stop at margin_ns.
	if (is_within_of_each_other(  //
	        f->present_margin_ns, //
	        dt->margin_ns,        //
	        dt->adjust_non_miss_ns)) {
		// Nothing to do, the GPU ended its work +-adjust_non_miss_ns
		// of margin_ns before the present started.
		return;
	}

	// We didn't miss the frame but we were outside the range adjust the app time.
	if (f->present_margin_ns > dt->margin_ns) {
		// Approach the present time.
		dt->app_time_ns -= dt->adjust_non_miss_ns;
	} else {
		// Back off the present time.
		dt->app_time_ns += dt->adjust_non_miss_ns;
	}
}


/*
 *
 * Member functions.
 *
 */

static void
dt_predict(struct u_pacing_compositor *upc,
           int64_t *out_frame_id,
           uint64_t *out_wake_up_time_ns,
           uint64_t *out_desired_present_time_ns,
           uint64_t *out_present_slop_ns,
           uint64_t *out_predicted_display_time_ns,
           uint64_t *out_predicted_display_period_ns,
           uint64_t *out_min_display_period_ns)
{
	struct display_timing *dt = display_timing(upc);

	struct frame *f = predict_next_frame(dt);

	uint64_t wake_up_time_ns = f->wake_up_time_ns;
	uint64_t desired_present_time_ns = f->desired_present_time_ns;
	uint64_t present_slop_ns = U_TIME_HALF_MS_IN_NS;
	uint64_t predicted_display_time_ns = f->predicted_display_time_ns;
	uint64_t predicted_display_period_ns = dt->frame_period_ns;
	uint64_t min_display_period_ns = dt->frame_period_ns;

	*out_frame_id = f->frame_id;
	*out_wake_up_time_ns = wake_up_time_ns;
	*out_desired_present_time_ns = desired_present_time_ns;
	*out_present_slop_ns = present_slop_ns;
	*out_predicted_display_time_ns = predicted_display_time_ns;
	*out_predicted_display_period_ns = predicted_display_period_ns;
	*out_min_display_period_ns = min_display_period_ns;
}

static void
dt_mark_point(struct u_pacing_compositor *upc, enum u_timing_point point, int64_t frame_id, uint64_t when_ns)
{
	struct display_timing *dt = display_timing(upc);
	struct frame *f = get_frame(dt, frame_id);

	switch (point) {
	case U_TIMING_POINT_WAKE_UP:
		assert(f->state == STATE_PREDICTED);
		f->state = STATE_WOKE;
		f->when_woke_ns = when_ns;
		break;
	case U_TIMING_POINT_BEGIN:
		assert(f->state == STATE_WOKE);
		f->state = STATE_BEGAN;
		f->when_began_ns = when_ns;
		break;
	case U_TIMING_POINT_SUBMIT:
		assert(f->state == STATE_BEGAN);
		f->state = STATE_SUBMITTED;
		f->when_submitted_ns = when_ns;
		break;
	default: assert(false);
	}
}

static void
dt_info(struct u_pacing_compositor *upc,
        int64_t frame_id,
        uint64_t desired_present_time_ns,
        uint64_t actual_present_time_ns,
        uint64_t earliest_present_time_ns,
        uint64_t present_margin_ns)
{
	struct display_timing *dt = display_timing(upc);
	(void)dt;

	struct frame *last = get_latest_frame_with_state_at_least(dt, STATE_INFO);
	struct frame *f = get_frame(dt, frame_id);
	if (f->frame_id != frame_id) {
		FT_LOG_W("Discarded info for unsubmitted or expired frame_id %" PRIx64, frame_id);
		if (last != NULL) {
			FT_LOG_W("The latest frame_id we have info for is %" PRIx64, last->frame_id);
		}
		return;
	}
	assert(f->state == STATE_SUBMITTED);
	assert(f->desired_present_time_ns == desired_present_time_ns);

	f->when_infoed_ns = os_monotonic_get_ns();
	f->actual_present_time_ns = actual_present_time_ns;
	f->earliest_present_time_ns = earliest_present_time_ns;
	f->present_margin_ns = present_margin_ns;
	f->state = STATE_INFO;

	uint64_t since_last_frame_ns = 0;
	if (last != NULL) {
		since_last_frame_ns = f->desired_present_time_ns - last->desired_present_time_ns;
	}

	// Adjust the frame timing.
	adjust_app_time(dt, f);

	double present_margin_ms = ns_to_ms(present_margin_ns);
	double since_last_frame_ms = ns_to_ms(since_last_frame_ns);

	FT_LOG_T(
	    "Got"
	    "\n\tframe_id:                 0x%08" PRIx64 //
	    "\n\twhen_predict_ns:          %" PRIu64     //
	    "\n\twhen_woke_ns:             %" PRIu64     //
	    "\n\twhen_submitted_ns:        %" PRIu64     //
	    "\n\twhen_infoed_ns:           %" PRIu64     //
	    "\n\tsince_last_frame_ms:      %.2fms"       //
	    "\n\tdesired_present_time_ns:  %" PRIu64     //
	    "\n\tactual_present_time_ns:   %" PRIu64     //
	    "\n\tearliest_present_time_ns: %" PRIu64     //
	    "\n\tpresent_margin_ns:        %" PRIu64     //
	    "\n\tpresent_margin_ms:        %.2fms",      //
	    frame_id,                                    //
	    f->when_predict_ns,                          //
	    f->when_woke_ns,                             //
	    f->when_submitted_ns,                        //
	    f->when_infoed_ns,                           //
	    since_last_frame_ms,                         //
	    f->desired_present_time_ns,                  //
	    f->actual_present_time_ns,                   //
	    f->earliest_present_time_ns,                 //
	    f->present_margin_ns,                        //
	    present_margin_ms);                          //


	if (!U_TRACE_CATEGORY_IS_ENABLED(timing)) {
		return;
	}

#define TE_BEG(TRACK, TIME, NAME) U_TRACE_EVENT_BEGIN_ON_TRACK_DATA(timing, TRACK, TIME, NAME, PERCETTO_I(f->frame_id))
#define TE_END(TRACK, TIME) U_TRACE_EVENT_END_ON_TRACK(timing, TRACK, TIME)


	/*
	 *
	 * CPU
	 *
	 */

	TE_BEG(pc_cpu, f->when_predict_ns, "sleep");
	TE_END(pc_cpu, f->wake_up_time_ns);

	uint64_t oversleep_start_ns = f->wake_up_time_ns + 1;
	if (f->when_woke_ns > oversleep_start_ns) {
		TE_BEG(pc_cpu, oversleep_start_ns, "oversleep");
		TE_END(pc_cpu, f->when_woke_ns);
	}


	/*
	 *
	 * GPU
	 *
	 */

	uint64_t gpu_end_ns = f->actual_present_time_ns - f->present_margin_ns;
	if (gpu_end_ns > f->when_submitted_ns) {
		TE_BEG(pc_gpu, f->when_submitted_ns, "gpu");
		TE_END(pc_gpu, gpu_end_ns);
	} else {
		TE_BEG(pc_gpu, gpu_end_ns, "gpu-time-travel");
		TE_END(pc_gpu, f->when_submitted_ns);
	}


	/*
	 *
	 * Margin
	 *
	 */

	if (gpu_end_ns < f->desired_present_time_ns) {
		TE_BEG(pc_margin, gpu_end_ns, "margin");
		TE_END(pc_margin, f->desired_present_time_ns);
	}


	/*
	 *
	 * ERROR
	 *
	 */

	if (!is_within_half_ms(f->actual_present_time_ns, f->desired_present_time_ns)) {
		if (f->actual_present_time_ns > f->desired_present_time_ns) {
			TE_BEG(pc_error, f->desired_present_time_ns, "slippage");
			TE_END(pc_error, f->actual_present_time_ns);
		} else {
			TE_BEG(pc_error, f->actual_present_time_ns, "run-ahead");
			TE_END(pc_error, f->desired_present_time_ns);
		}
	}


	/*
	 *
	 * Info
	 *
	 */

	if (f->when_infoed_ns >= f->actual_present_time_ns) {
		TE_BEG(pc_info, f->actual_present_time_ns, "info");
		TE_END(pc_info, f->when_infoed_ns);
	} else {
		TE_BEG(pc_info, f->when_infoed_ns, "info_before");
		TE_END(pc_info, f->actual_present_time_ns);
	}


	/*
	 *
	 * Present
	 *
	 */

	if (f->actual_present_time_ns != f->earliest_present_time_ns) {
		U_TRACE_INSTANT_ON_TRACK(timing, pc_present, f->earliest_present_time_ns, "earliest");
	}
	if (!is_within_half_ms(f->desired_present_time_ns, f->earliest_present_time_ns)) {
		U_TRACE_INSTANT_ON_TRACK(timing, pc_present, f->desired_present_time_ns, "predicted");
	}
	U_TRACE_INSTANT_ON_TRACK(timing, pc_present, f->actual_present_time_ns, "vsync");


	/*
	 *
	 * Compositor time
	 *
	 */

	TE_BEG(pc_allotted, f->wake_up_time_ns, "allotted");
	TE_END(pc_allotted, f->wake_up_time_ns + f->current_app_time_ns);

#undef TE_BEG
#undef TE_END
}

static void
dt_destroy(struct u_pacing_compositor *upc)
{
	struct display_timing *dt = display_timing(upc);

	free(dt);
}

const struct u_pc_display_timing_config U_PC_DISPLAY_TIMING_CONFIG_DEFAULT = {
    // Just a wild guess.
    .present_offset_ns = U_TIME_1MS_IN_NS * 4,
    .margin_ns = U_TIME_1MS_IN_NS,
    .app_time_fraction = 10,
    .app_time_max_fraction = 30,
    .adjust_missed_fraction = 4,
    .adjust_non_miss_fraction = 2,
};

xrt_result_t
u_pc_display_timing_create(uint64_t estimated_frame_period_ns,
                           const struct u_pc_display_timing_config *config,
                           struct u_pacing_compositor **out_uft)
{
	struct display_timing *dt = U_TYPED_CALLOC(struct display_timing);
	dt->base.predict = dt_predict;
	dt->base.mark_point = dt_mark_point;
	dt->base.info = dt_info;
	dt->base.destroy = dt_destroy;
	dt->frame_period_ns = estimated_frame_period_ns;

	// Estimate of how long after "present" the photons hit the eyes
	dt->present_offset_ns = config->present_offset_ns;

	// Start at this of frame time.
	dt->app_time_ns = get_percent_of_time(estimated_frame_period_ns, config->app_time_fraction);
	// Max app time, write a better compositor.
	dt->app_time_max_ns = get_percent_of_time(estimated_frame_period_ns, config->app_time_max_fraction);
	// When missing, back off in these increments
	dt->adjust_missed_ns = get_percent_of_time(estimated_frame_period_ns, config->adjust_missed_fraction);
	// When not missing frames but adjusting app time at these increments
	dt->adjust_non_miss_ns = get_percent_of_time(estimated_frame_period_ns, config->adjust_non_miss_fraction);
	// Extra margin that is added to app time.
	dt->margin_ns = config->margin_ns;

	*out_uft = &dt->base;

	double estimated_frame_period_ms = ns_to_ms(estimated_frame_period_ns);
	FT_LOG_I("Created display timing (%.2fms)", estimated_frame_period_ms);

	return XRT_SUCCESS;
}
