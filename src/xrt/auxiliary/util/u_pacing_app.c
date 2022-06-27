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
#include "util/u_pacing.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

DEBUG_GET_ONCE_LOG_OPTION(log_level, "U_PACING_APP_LOG", U_LOGGING_WARN)

#define UPA_LOG_T(...) U_LOG_IFL_T(debug_get_log_option_log_level(), __VA_ARGS__)
#define UPA_LOG_D(...) U_LOG_IFL_D(debug_get_log_option_log_level(), __VA_ARGS__)
#define UPA_LOG_I(...) U_LOG_IFL_I(debug_get_log_option_log_level(), __VA_ARGS__)
#define UPA_LOG_W(...) U_LOG_IFL_W(debug_get_log_option_log_level(), __VA_ARGS__)
#define UPA_LOG_E(...) U_LOG_IFL_E(debug_get_log_option_log_level(), __VA_ARGS__)


/*
 *
 * Structs enums, and defines.
 *
 */

#define FRAME_COUNT (8)

enum u_pa_state
{
	U_PA_READY,
	U_RT_WAIT_LEFT,
	U_RT_PREDICTED,
	U_RT_BEGUN,
	U_RT_DELIVERED,
};

struct u_pa_frame
{
	int64_t frame_id;

	//! When we predicted this frame to be shown.
	uint64_t predicted_display_time_ns;

	//! The selected display period.
	uint64_t predicted_display_period_ns;

	//! When the client's GPU work should have completed.
	uint64_t predicted_gpu_done_time_ns;

	/*!
	 * When the app told us to display this frame, can be different
	 * then the predicted display time so we track that separately.
	 */
	uint64_t display_time_ns;

	//! When something happened.
	struct
	{
		uint64_t predicted_ns;
		uint64_t wait_woke_ns;
		uint64_t begin_ns;
		uint64_t delivered_ns;
		uint64_t gpu_done_ns;
	} when;

	enum u_pa_state state;
};

struct pacing_app
{
	struct u_pacing_app base;

	struct u_pa_frame frames[FRAME_COUNT];
	uint32_t current_frame;
	uint32_t next_frame;

	int64_t frame_counter;

	struct
	{
		//! App time between wait returning and begin being called.
		uint64_t cpu_time_ns;
		//! Time between begin and frame data being delivered.
		uint64_t draw_time_ns;
		//! Time between the frame data being delivered and GPU completing.
		uint64_t wait_time_ns;
		//! Extra time between end of draw time and when the compositor wakes up.
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


/*
 *
 * Helpers
 *
 */

static inline struct pacing_app *
pacing_app(struct u_pacing_app *upa)
{
	return (struct pacing_app *)upa;
}

#define DEBUG_PRINT_FRAME_ID() UPA_LOG_T("%" PRIi64, frame_id)
#define GET_INDEX_FROM_ID(RT, ID) ((uint64_t)(ID) % FRAME_COUNT)

#define IIR_ALPHA_LT 0.8
#define IIR_ALPHA_GT 0.8

static void
do_iir_filter(uint64_t *target, double alpha_lt, double alpha_gt, uint64_t sample)
{
	uint64_t t = *target;
	double alpha = t < sample ? alpha_lt : alpha_gt;
	double a = time_ns_to_s(t) * alpha;
	double b = time_ns_to_s(sample) * (1.0 - alpha);
	*target = time_s_to_ns(a + b);
}

static uint64_t
min_period(const struct pacing_app *pa)
{
	return pa->last_input.predicted_display_period_ns;
}

static uint64_t
last_sample_displayed(const struct pacing_app *pa)
{
	return pa->last_input.predicted_display_time_ns;
}

static uint64_t
last_return_predicted_display(const struct pacing_app *pa)
{
	return pa->last_returned_ns;
}

static uint64_t
total_app_time_ns(const struct pacing_app *pa)
{
	return pa->app.cpu_time_ns + pa->app.draw_time_ns + pa->app.wait_time_ns;
}

static uint64_t
total_compositor_time_ns(const struct pacing_app *pa)
{
	return pa->app.margin_ns + pa->last_input.extra_ns;
}

static uint64_t
total_app_and_compositor_time_ns(const struct pacing_app *pa)
{
	return total_app_time_ns(pa) + total_compositor_time_ns(pa);
}

static uint64_t
calc_period(const struct pacing_app *pa)
{
	// Error checking.
	uint64_t base_period_ns = min_period(pa);
	if (base_period_ns == 0) {
		assert(false && "Have not yet received and samples from timing driver.");
		base_period_ns = U_TIME_1MS_IN_NS * 16; // Sure
	}

	// Calculate the using both values separately.
	uint64_t period_ns = base_period_ns;
	while (pa->app.cpu_time_ns > period_ns) {
		period_ns += base_period_ns;
	}

	while (pa->app.draw_time_ns > period_ns) {
		period_ns += base_period_ns;
	}

	while (pa->app.wait_time_ns > period_ns) {
		period_ns += base_period_ns;
	}

	return period_ns;
}

static uint64_t
predict_display_time(const struct pacing_app *pa, uint64_t now_ns, uint64_t period_ns)
{

	// Total app and compositor time to produce a frame
	uint64_t app_and_compositor_time_ns = total_app_and_compositor_time_ns(pa);

	// Start from the last time that the driver displayed something.
	uint64_t val = last_sample_displayed(pa);

	// Return a time after the last returned display time. Add half the
	// display period to the comparison for robustness when the last display
	// time shifts slightly with respect to the last sample.
	while (val <= last_return_predicted_display(pa) + (period_ns / 2)) {
		val += period_ns;
	}

	// Have to have enough time to perform app work.
	while ((val - app_and_compositor_time_ns) <= now_ns) {
		val += period_ns;
	}

	return val;
}


/*
 *
 * Member functions.
 *
 */

static void
pa_predict(struct u_pacing_app *upa,
           uint64_t now_ns,
           int64_t *out_frame_id,
           uint64_t *out_wake_up_time,
           uint64_t *out_predicted_display_time,
           uint64_t *out_predicted_display_period)
{
	struct pacing_app *pa = pacing_app(upa);

	int64_t frame_id = ++pa->frame_counter;
	*out_frame_id = frame_id;

	DEBUG_PRINT_FRAME_ID();

	uint64_t period_ns = calc_period(pa);
	uint64_t predict_ns = predict_display_time(pa, now_ns, period_ns);
	// When should the client wake up.
	uint64_t wake_up_time_ns = predict_ns - total_app_and_compositor_time_ns(pa);
	// When the client's GPU work should have completed.
	uint64_t gpu_done_time_ns = predict_ns - total_compositor_time_ns(pa);

	pa->last_returned_ns = predict_ns;

	*out_wake_up_time = wake_up_time_ns;
	*out_predicted_display_time = predict_ns;
	*out_predicted_display_period = period_ns;

	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	assert(pa->frames[index].frame_id == -1);
	assert(pa->frames[index].state == U_PA_READY);

	pa->frames[index].state = U_RT_PREDICTED;
	pa->frames[index].frame_id = frame_id;
	pa->frames[index].predicted_gpu_done_time_ns = gpu_done_time_ns;
	pa->frames[index].predicted_display_period_ns = period_ns;
	pa->frames[index].when.predicted_ns = now_ns;
}

static void
pa_mark_point(struct u_pacing_app *upa, int64_t frame_id, enum u_timing_point point, uint64_t when_ns)
{
	struct pacing_app *pa = pacing_app(upa);

	UPA_LOG_T("%" PRIi64 " (%u)", frame_id, point);

	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	assert(pa->frames[index].frame_id == frame_id);

	switch (point) {
	case U_TIMING_POINT_WAKE_UP:
		assert(pa->frames[index].state == U_RT_PREDICTED);

		pa->frames[index].when.wait_woke_ns = when_ns;
		pa->frames[index].state = U_RT_WAIT_LEFT;
		break;
	case U_TIMING_POINT_BEGIN:
		assert(pa->frames[index].state == U_RT_WAIT_LEFT);

		pa->frames[index].when.begin_ns = when_ns;
		pa->frames[index].state = U_RT_BEGUN;
		break;
	case U_TIMING_POINT_SUBMIT:
	default: assert(false);
	}
}

static void
pa_mark_discarded(struct u_pacing_app *upa, int64_t frame_id, uint64_t when_ns)
{
	struct pacing_app *pa = pacing_app(upa);

	DEBUG_PRINT_FRAME_ID();

	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	assert(pa->frames[index].frame_id == frame_id);
	assert(pa->frames[index].state == U_RT_WAIT_LEFT || pa->frames[index].state == U_RT_BEGUN);

	pa->frames[index].when.delivered_ns = when_ns;
	pa->frames[index].state = U_PA_READY;
	pa->frames[index].frame_id = -1;
}

static void
pa_mark_delivered(struct u_pacing_app *upa, int64_t frame_id, uint64_t when_ns, uint64_t display_time_ns)
{
	struct pacing_app *pa = pacing_app(upa);

	DEBUG_PRINT_FRAME_ID();

	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	struct u_pa_frame *f = &pa->frames[index];
	assert(f->frame_id == frame_id);
	assert(f->state == U_RT_BEGUN);

	f->when.delivered_ns = when_ns;
	f->display_time_ns = display_time_ns;
	f->state = U_RT_DELIVERED;
}

static void
pa_mark_gpu_done(struct u_pacing_app *upa, int64_t frame_id, uint64_t when_ns)
{
	struct pacing_app *pa = pacing_app(upa);

	DEBUG_PRINT_FRAME_ID();

	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	struct u_pa_frame *f = &pa->frames[index];
	assert(f->frame_id == frame_id);
	assert(f->state == U_RT_DELIVERED);

	// Update all data.
	f->when.gpu_done_ns = when_ns;


	/*
	 * Process data.
	 */

	int64_t diff_ns = f->predicted_gpu_done_time_ns - when_ns;
	bool late = false;
	if (diff_ns < 0) {
		diff_ns = -diff_ns;
		late = true;
	}

#define NS_TO_MS_F(ns) (time_ns_to_s(ns) * 1000.0)

	uint64_t diff_cpu_ns = f->when.begin_ns - f->when.wait_woke_ns;
	uint64_t diff_draw_ns = f->when.delivered_ns - f->when.begin_ns;
	uint64_t diff_wait_ns = f->when.gpu_done_ns - f->when.delivered_ns;

	UPA_LOG_D(
	    "Delivered frame %.2fms %s."                                           //
	    "\n\tperiod: %.2f"                                                     //
	    "\n\tcpu  o: %.2f, n: %.2f"                                            //
	    "\n\tdraw o: %.2f, n: %.2f"                                            //
	    "\n\twait o: %.2f, n: %.2f",                                           //
	    time_ns_to_ms_f(diff_ns), late ? "late" : "early",                     //
	    time_ns_to_ms_f(f->predicted_display_period_ns),                       //
	    time_ns_to_ms_f(pa->app.cpu_time_ns), time_ns_to_ms_f(diff_cpu_ns),    //
	    time_ns_to_ms_f(pa->app.draw_time_ns), time_ns_to_ms_f(diff_draw_ns),  //
	    time_ns_to_ms_f(pa->app.wait_time_ns), time_ns_to_ms_f(diff_wait_ns)); //

	do_iir_filter(&pa->app.cpu_time_ns, IIR_ALPHA_LT, IIR_ALPHA_GT, diff_cpu_ns);
	do_iir_filter(&pa->app.draw_time_ns, IIR_ALPHA_LT, IIR_ALPHA_GT, diff_draw_ns);
	do_iir_filter(&pa->app.wait_time_ns, IIR_ALPHA_LT, IIR_ALPHA_GT, diff_wait_ns);

	// Trace the data.
#ifdef XRT_FEATURE_TRACING
#define TE_BEG(TRACK, TIME, NAME) U_TRACE_EVENT_BEGIN_ON_TRACK_DATA(timing, TRACK, TIME, NAME, PERCETTO_I(f->frame_id))
#define TE_END(TRACK, TIME) U_TRACE_EVENT_END_ON_TRACK(timing, TRACK, TIME)

	if (U_TRACE_CATEGORY_IS_ENABLED(timing)) {
		TE_BEG(pa_cpu, f->when.predicted_ns, "sleep");
		TE_END(pa_cpu, f->when.wait_woke_ns);

		uint64_t cpu_start_ns = f->when.wait_woke_ns + 1;
		TE_BEG(pa_cpu, cpu_start_ns, "cpu");
		TE_END(pa_cpu, f->when.begin_ns);

		TE_BEG(pa_draw, f->when.begin_ns, "draw");
		if (f->when.begin_ns > f->predicted_gpu_done_time_ns) {
			TE_BEG(pa_draw, f->when.begin_ns, "late");
			TE_END(pa_draw, f->when.delivered_ns);
		} else if (f->when.delivered_ns > f->predicted_gpu_done_time_ns) {
			TE_BEG(pa_draw, f->predicted_gpu_done_time_ns, "late");
			TE_END(pa_draw, f->when.delivered_ns);
		}
		TE_END(pa_draw, f->when.delivered_ns);

		TE_BEG(pa_wait, f->when.delivered_ns, "wait");
		if (f->when.delivered_ns > f->predicted_gpu_done_time_ns) {
			TE_BEG(pa_wait, f->when.delivered_ns, "late");
			TE_END(pa_wait, f->when.gpu_done_ns);
		} else if (f->when.delivered_ns > f->predicted_gpu_done_time_ns) {
			TE_BEG(pa_wait, f->predicted_gpu_done_time_ns, "late");
			TE_END(pa_wait, f->when.gpu_done_ns);
		}
		TE_END(pa_wait, f->when.gpu_done_ns);
	}

#undef TE_BEG
#undef TE_END
#endif

	// Reset the frame.
	f->state = U_PA_READY;
	f->frame_id = -1;
}

static void
pa_info(struct u_pacing_app *upa,
        uint64_t predicted_display_time_ns,
        uint64_t predicted_display_period_ns,
        uint64_t extra_ns)
{
	struct pacing_app *pa = pacing_app(upa);

	pa->last_input.predicted_display_time_ns = predicted_display_time_ns;
	pa->last_input.predicted_display_period_ns = predicted_display_period_ns;
	pa->last_input.extra_ns = extra_ns;
}

static void
pa_destroy(struct u_pacing_app *upa)
{
	free(upa);
}

static xrt_result_t
pa_create(struct u_pacing_app **out_upa)
{
	struct pacing_app *pa = U_TYPED_CALLOC(struct pacing_app);
	pa->base.predict = pa_predict;
	pa->base.mark_point = pa_mark_point;
	pa->base.mark_discarded = pa_mark_discarded;
	pa->base.mark_delivered = pa_mark_delivered;
	pa->base.mark_gpu_done = pa_mark_gpu_done;
	pa->base.info = pa_info;
	pa->base.destroy = pa_destroy;
	pa->app.cpu_time_ns = U_TIME_1MS_IN_NS * 2;
	pa->app.draw_time_ns = U_TIME_1MS_IN_NS * 2;
	pa->app.margin_ns = U_TIME_1MS_IN_NS * 2;

	for (size_t i = 0; i < ARRAY_SIZE(pa->frames); i++) {
		pa->frames[i].state = U_PA_READY;
		pa->frames[i].frame_id = -1;
	}

	*out_upa = &pa->base;

	return XRT_SUCCESS;
}


/*
 *
 * Factory functions.
 *
 */

static xrt_result_t
paf_create(struct u_pacing_app_factory *upaf, struct u_pacing_app **out_upa)
{
	return pa_create(out_upa);
}

static void
paf_destroy(struct u_pacing_app_factory *upaf)
{
	free(upaf);
}


/*
 *
 * 'Exported' functions.
 *
 */

xrt_result_t
u_pa_factory_create(struct u_pacing_app_factory **out_upaf)
{
	struct u_pacing_app_factory *upaf = U_TYPED_CALLOC(struct u_pacing_app_factory);
	upaf->create = paf_create;
	upaf->destroy = paf_destroy;

	*out_upaf = upaf;

	return XRT_SUCCESS;
}
