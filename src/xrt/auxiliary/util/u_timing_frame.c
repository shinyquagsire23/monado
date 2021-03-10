// Copyright 2020-2021, Collabora, Ltd.
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
#include "util/u_timing.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

DEBUG_GET_ONCE_LOG_OPTION(ll, "U_TIMING_FRAME_LOG", U_LOGGING_WARN)

#define FT_LOG_T(...) U_LOG_IFL_T(debug_get_log_option_ll(), __VA_ARGS__)
#define FT_LOG_D(...) U_LOG_IFL_D(debug_get_log_option_ll(), __VA_ARGS__)
#define FT_LOG_I(...) U_LOG_IFL_I(debug_get_log_option_ll(), __VA_ARGS__)
#define FT_LOG_W(...) U_LOG_IFL_W(debug_get_log_option_ll(), __VA_ARGS__)
#define FT_LOG_E(...) U_LOG_IFL_E(debug_get_log_option_ll(), __VA_ARGS__)

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
	int64_t frame_id;
	uint64_t when_predict_ns;
	uint64_t wake_up_time_ns;
	uint64_t when_woke_ns;
	uint64_t when_began_ns;
	uint64_t when_submitted_ns;
	uint64_t when_infoed_ns;
	uint64_t desired_present_time_ns;
	uint64_t predicted_display_time_ns;
	uint64_t present_margin_ns;
	uint64_t actual_present_time_ns;
	uint64_t earliest_present_time_ns;

	enum frame_state state;
};

struct display_timing
{
	struct u_frame_timing base;

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
	 * The target amount of GPU margin we want.
	 */
	uint64_t adjust_min_margin_ns;

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
display_timing(struct u_frame_timing *uft)
{
	return (struct display_timing *)uft;
}

static double
ns_to_ms(int64_t t)
{
	return (double)(t / 1000) / 1000.0;
}

static uint64_t
get_procent_of_time(uint64_t time_ns, uint32_t fraction_procent)
{
	double fraction = (double)fraction_procent / 100.0;
	return time_s_to_ns(time_ns_to_s(time_ns) * fraction);
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

static struct frame *
get_frame(struct display_timing *dt, int64_t frame_id)
{
	assert(frame_id >= 0);
	assert((uint64_t)frame_id <= (uint64_t)SIZE_MAX);

	size_t index = (size_t)(frame_id % NUM_FRAMES);

	return &dt->frames[index];
}

static struct frame *
create_frame(struct display_timing *dt, enum frame_state state)
{
	int64_t frame_id = dt->next_frame_id++;
	struct frame *f = get_frame(dt, frame_id);

	f->frame_id = frame_id;
	f->state = state;

	return f;
}

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

static struct frame *
do_clean_slate_frame(struct display_timing *dt)
{
	struct frame *f = create_frame(dt, STATE_PREDICTED);
	uint64_t now_ns = os_monotonic_get_ns();

	// Wild shot in the dark.
	uint64_t the_time_ns = os_monotonic_get_ns() + dt->frame_period_ns * 10;
	f->when_predict_ns = now_ns;
	f->desired_present_time_ns = the_time_ns;
	f->predicted_display_time_ns = calc_display_time_from_present_time(dt, the_time_ns);

	return f;
}

static struct frame *
walk_forward_through_frames(struct display_timing *dt, uint64_t last_present_time_ns)
{
	uint64_t now_ns = os_monotonic_get_ns();
	uint64_t from_time_ns = now_ns + dt->app_time_ns;
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
	f->predicted_display_time_ns = calc_display_time_from_present_time(dt, desired_present_time_ns);

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

	f->wake_up_time_ns = f->desired_present_time_ns - dt->app_time_ns;

	return f;
}

static void
adjust_app_time(struct display_timing *dt, struct frame *f)
{
	uint64_t app_time_ns = dt->app_time_ns;

	if (f->actual_present_time_ns > f->desired_present_time_ns &&
	    !is_within_half_ms(f->actual_present_time_ns, f->desired_present_time_ns)) {
		double missed_ms = ns_to_ms(f->actual_present_time_ns - f->desired_present_time_ns);
		FT_LOG_D("Missed by %.2f!", missed_ms);

		app_time_ns += dt->adjust_missed_ns;
		if (app_time_ns > dt->app_time_max_ns) {
			app_time_ns = dt->app_time_max_ns;
		}

		dt->app_time_ns = app_time_ns;
		return;
	}

	// We want the GPU work to stop at adjust_min_margin_ns.
	if (is_within_of_each_other(      //
	        f->present_margin_ns,     //
	        dt->adjust_min_margin_ns, //
	        dt->adjust_non_miss_ns)) {
		// Nothing to do, the GPU ended it's work +-adjust_non_miss_ns
		// of adjust_min_margin_ns before the present started.
		return;
	}

	// We didn't miss the frame but we were outside the range adjust the app time.
	if (f->present_margin_ns > dt->adjust_min_margin_ns) {
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
dt_predict(struct u_frame_timing *uft,
           int64_t *out_frame_id,
           uint64_t *out_wake_up_time_ns,
           uint64_t *out_desired_present_time_ns,
           uint64_t *out_present_slop_ns,
           uint64_t *out_predicted_display_time_ns,
           uint64_t *out_predicted_display_period_ns,
           uint64_t *out_min_display_period_ns)
{
	struct display_timing *dt = display_timing(uft);

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
dt_mark_point(struct u_frame_timing *uft, enum u_timing_point point, int64_t frame_id, uint64_t when_ns)
{
	struct display_timing *dt = display_timing(uft);
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
dt_info(struct u_frame_timing *uft,
        int64_t frame_id,
        uint64_t desired_present_time_ns,
        uint64_t actual_present_time_ns,
        uint64_t earliest_present_time_ns,
        uint64_t present_margin_ns)
{
	struct display_timing *dt = display_timing(uft);
	(void)dt;

	struct frame *last = get_latest_frame_with_state_at_least(dt, STATE_INFO);
	struct frame *f = get_frame(dt, frame_id);
	assert(f->state == STATE_SUBMITTED);

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

	COMP_TRACE_DATA(U_TRACE_DATA_TYPE_TIMING_FRAME, *f);
}

static void
dt_destroy(struct u_frame_timing *uft)
{
	struct display_timing *dt = display_timing(uft);

	free(dt);
}

xrt_result_t
u_frame_timing_display_timing_create(uint64_t estimated_frame_period_ns, struct u_frame_timing **out_uft)
{
	struct display_timing *dt = U_TYPED_CALLOC(struct display_timing);
	dt->base.predict = dt_predict;
	dt->base.mark_point = dt_mark_point;
	dt->base.info = dt_info;
	dt->base.destroy = dt_destroy;
	dt->frame_period_ns = estimated_frame_period_ns;

	// Just a wild guess.
	dt->present_offset_ns = U_TIME_1MS_IN_NS * 4;

	// Start at 40% of the frame time, will be adjusted.
	dt->app_time_ns = get_procent_of_time(estimated_frame_period_ns, 40);
	// Max app time at 80%, write a better compositor.
	dt->app_time_max_ns = get_procent_of_time(estimated_frame_period_ns, 80);
	// When missing back off at 10% increments
	dt->adjust_missed_ns = get_procent_of_time(estimated_frame_period_ns, 10);
	// When not missing frames but adjusting app time do it at 2% increments
	dt->adjust_non_miss_ns = get_procent_of_time(estimated_frame_period_ns, 2);
	// Min margin at 8%
	dt->adjust_min_margin_ns = get_procent_of_time(estimated_frame_period_ns, 8);

	*out_uft = &dt->base;

	FT_LOG_I("Created display timing");

	return XRT_SUCCESS;
}


/*
 *
 * Tracing functions.
 *
 */

#define TID_NORMAL 43
#define TID_GPU 44
#define TID_INFO 45
#define TID_FRAME 46
#define TID_ERROR 47

XRT_MAYBE_UNUSED static void
trace_event(FILE *file, const char *name, uint64_t when_ns)
{
	if (file == NULL) {
		return;
	}

	// clang-format off
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"i\",\n"
	        "\t\t\t\"name\": \"%s\",\n"
	        "\t\t\t\"ts\": %" PRIu64 ".%03" PRIu64 ",\n"
	        "\t\t\t\"pid\": 42,\n"
	        "\t\t\t\"tid\": 43,\n"
	        "\t\t\t\"s\": \"g\",\n"
	        "\t\t\t\"args\": {}\n"
	        "\t\t}",
	        name, when_ns / 1000, when_ns % 1000);
	// clang-format off
}

static void
trace_event_id(FILE *file, const char *name, int64_t frame_id, uint64_t when_ns)
{
	if (file == NULL) {
		return;
	}

	// clang-format off
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"i\",\n"
	        "\t\t\t\"name\": \"%s\",\n"
	        "\t\t\t\"ts\": %" PRIu64 ".%03" PRIu64 ",\n"
	        "\t\t\t\"pid\": 42,\n"
	        "\t\t\t\"tid\": 43,\n"
	        "\t\t\t\"s\": \"g\",\n"
	        "\t\t\t\"args\": {"
	        "\t\t\t\t\"id\": %" PRIi64 "\n"
	        "\t\t\t}\n"
	        "\t\t}",
	        name, when_ns / 1000, when_ns % 1000, frame_id);
	// clang-format off
}

static void
trace_begin(FILE *file, uint32_t tid, const char *name, const char *cat, uint64_t when_ns)
{
	if (file == NULL) {
		return;
	}

	// clang-format off
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"B\",\n"
	        "\t\t\t\"name\": \"%s\",\n"
	        "\t\t\t\"cat\": \"%s\",\n"
	        "\t\t\t\"ts\": %" PRIu64 ".%03" PRIu64 ",\n"
	        "\t\t\t\"pid\": 42,\n"
	        "\t\t\t\"tid\": %u,\n"
	        "\t\t\t\"args\": {}\n"
	        "\t\t}",
	        name, cat, when_ns / 1000, when_ns % 1000, tid);
	// clang-format on
}

static void
trace_begin_id(FILE *file, uint32_t tid, const char *name, int64_t frame_id, const char *cat, uint64_t when_ns)
{
	if (file == NULL) {
		return;
	}

	char temp[256];
	snprintf(temp, sizeof(temp), "%s %" PRIi64, name, frame_id);

	trace_begin(file, tid, temp, cat, when_ns);
}

static void
trace_end(FILE *file, uint32_t tid, uint64_t when_ns)
{
	if (file == NULL) {
		return;
	}

	// clang-format off
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"E\",\n"
	        "\t\t\t\"ts\": %" PRIu64 ".%03" PRIu64 ",\n"
	        "\t\t\t\"pid\": 42,\n"
	        "\t\t\t\"tid\": %u,\n"
	        "\t\t\t\"args\": {}\n"
	        "\t\t}",
	        when_ns / 1000, when_ns % 1000, tid);
	// clang-format on
}

static void
trace_frame(FILE *file, struct frame *f)
{
	trace_begin_id(file, TID_NORMAL, "sleep", f->frame_id, "sleep", f->when_predict_ns);
	trace_end(file, TID_NORMAL, f->wake_up_time_ns);

	if (f->when_woke_ns > f->wake_up_time_ns) {
		trace_begin_id(file, TID_NORMAL, "oversleep", f->frame_id, "sleep", f->wake_up_time_ns);
		trace_end(file, TID_NORMAL, f->when_woke_ns);
	}

	if (!is_within_half_ms(f->actual_present_time_ns, f->desired_present_time_ns)) {
		if (f->actual_present_time_ns > f->desired_present_time_ns) {
			trace_begin_id(file, TID_ERROR, "slippage", f->frame_id, "slippage",
			               f->desired_present_time_ns);
			trace_end(file, TID_ERROR, f->actual_present_time_ns);
		} else {
			trace_begin_id(file, TID_ERROR, "run-ahead", f->frame_id, "run-ahead",
			               f->actual_present_time_ns);
			trace_end(file, TID_ERROR, f->desired_present_time_ns);
		}
	}

	uint64_t gpu_end_ns = f->actual_present_time_ns - f->present_margin_ns;
	if (gpu_end_ns > f->when_submitted_ns) {
		trace_begin_id(file, TID_GPU, "gpu", f->frame_id, "gpu", f->when_submitted_ns);
		trace_end(file, TID_GPU, gpu_end_ns);
	} else {
		trace_begin_id(file, TID_GPU, "gpu-time-travel", f->frame_id, "gpu-time-travel", gpu_end_ns);
		trace_end(file, TID_GPU, f->when_submitted_ns);
	}

	if (f->when_infoed_ns >= f->actual_present_time_ns) {
		trace_begin_id(file, TID_INFO, "info", f->frame_id, "info", f->actual_present_time_ns);
		trace_end(file, TID_INFO, f->when_infoed_ns);
	} else {
		trace_begin_id(file, TID_INFO, "info before", f->frame_id, "info", f->when_infoed_ns);
		trace_end(file, TID_INFO, f->actual_present_time_ns);
	}

	trace_event_id(file, "vsync", f->frame_id, f->earliest_present_time_ns);
	if (f->actual_present_time_ns != f->earliest_present_time_ns) {
		trace_event_id(file, "flip", f->frame_id, f->actual_present_time_ns);
	}
}

void
u_timing_frame_write_json(FILE *file, void *data)
{
	trace_frame(file, (struct frame *)data);
}

void
u_timing_frame_write_json_metadata(FILE *file)
{
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"M\",\n"
	        "\t\t\t\"name\": \"thread_name\",\n"
	        "\t\t\t\"pid\": 42,\n"
	        "\t\t\t\"tid\": %u,\n"
	        "\t\t\t\"args\": {\n"
	        "\t\t\t\t\"name\": \"1 RendererThread\"\n"
	        "\t\t\t}\n"
	        "\t\t}",
	        TID_NORMAL);
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"M\",\n"
	        "\t\t\t\"name\": \"thread_name\",\n"
	        "\t\t\t\"pid\": 42,\n"
	        "\t\t\t\"tid\": %u,\n"
	        "\t\t\t\"args\": {\n"
	        "\t\t\t\t\"name\": \"2 GPU\"\n"
	        "\t\t\t}\n"
	        "\t\t}",
	        TID_GPU);
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"M\",\n"
	        "\t\t\t\"name\": \"thread_name\",\n"
	        "\t\t\t\"pid\": 42,\n"
	        "\t\t\t\"tid\": %u,\n"
	        "\t\t\t\"args\": {\n"
	        "\t\t\t\t\"name\": \"3 Info\"\n"
	        "\t\t\t}\n"
	        "\t\t}",
	        TID_INFO);
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"M\",\n"
	        "\t\t\t\"name\": \"thread_name\",\n"
	        "\t\t\t\"pid\": 42,\n"
	        "\t\t\t\"tid\": %u,\n"
	        "\t\t\t\"args\": {\n"
	        "\t\t\t\t\"name\": \"4 FrameTiming\"\n"
	        "\t\t\t}\n"
	        "\t\t}",
	        TID_FRAME);
	fprintf(file,
	        ",\n"
	        "\t\t{\n"
	        "\t\t\t\"ph\": \"M\",\n"
	        "\t\t\t\"name\": \"thread_name\",\n"
	        "\t\t\t\"pid\": 42,\n"
	        "\t\t\t\"tid\": %u,\n"
	        "\t\t\t\"args\": {\n"
	        "\t\t\t\t\"name\": \"5 Slips\"\n"
	        "\t\t\t}\n"
	        "\t\t}",
	        TID_ERROR);
	fflush(file);
}
