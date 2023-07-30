// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared frame timing code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "os/os_time.h"

#include "util/u_var.h"
#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_pacing.h"
#include "util/u_metrics.h"
#include "util/u_logging.h"
#include "util/u_trace_marker.h"

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

DEBUG_GET_ONCE_LOG_OPTION(log_level, "U_PACING_APP_LOG", U_LOGGING_WARN)
DEBUG_GET_ONCE_FLOAT_OPTION(min_app_time_ms, "U_PACING_APP_MIN_TIME_MS", 1.0f)

#define UPA_LOG_T(...) U_LOG_IFL_T(debug_get_log_option_log_level(), __VA_ARGS__)
#define UPA_LOG_D(...) U_LOG_IFL_D(debug_get_log_option_log_level(), __VA_ARGS__)
#define UPA_LOG_I(...) U_LOG_IFL_I(debug_get_log_option_log_level(), __VA_ARGS__)
#define UPA_LOG_W(...) U_LOG_IFL_W(debug_get_log_option_log_level(), __VA_ARGS__)
#define UPA_LOG_E(...) U_LOG_IFL_E(debug_get_log_option_log_level(), __VA_ARGS__)

/*!
 * Define to validate latched and retired call. Currently disabled due to
 * simplistic frame allocation code, enable once improved.
 */
#undef VALIDATE_LATCHED_AND_RETIRED


/*
 *
 * Structs enums, and defines.
 *
 */

/*!
 * This controls how many frames are in the allocation array.
 *
 * @todo The allocation code is not good, this is a work around for index reuse
 *       causing asserts, change the code so we don't need it at all.
 */
#define FRAME_COUNT (128)

enum u_pa_state
{
	U_PA_READY,
	U_RT_WAIT_LEFT,
	U_RT_PREDICTED,
	U_RT_BEGUN,
	U_RT_DELIVERED,
	U_RT_GPU_DONE,
};

struct u_pa_frame
{
	int64_t frame_id;

	//! How long we thought the frame would take.
	uint64_t predicted_frame_time_ns;

	//! When we predicted the app should wake up.
	uint64_t predicted_wake_up_time_ns;

	//! When the client's GPU work should have completed.
	uint64_t predicted_gpu_done_time_ns;

	//! When we predicted this frame to be shown.
	uint64_t predicted_display_time_ns;

	//! The selected display period.
	uint64_t predicted_display_period_ns;

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

	//! Id for this session.
	int64_t session_id;

	struct u_pa_frame frames[FRAME_COUNT];
	uint32_t current_frame;
	uint32_t next_frame;

	int64_t frame_counter;

	/*!
	 * Minimum calculated frame (total app time). Min app time lets you add
	 * of time between the time where the compositor picks the frame up and
	 * when the application is woken up. Essentially a minimum amount of
	 * latency between the app and the compositor (and by extension the
	 * display time).
	 *
	 * For applications that has varied frame times this lets the user tweak
	 * the values, trading latency for frame stability. Avoiding dropped
	 * frames, or jittery frame delivery.
	 *
	 * This does not effect frame cadence, you can essentially have 3x the
	 * frame periods time as latench but still run at frame cadence.
	 */
	struct u_var_draggable_f32 min_app_time_ms;

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

static inline const char *
state_to_str(enum u_pa_state state)
{
	switch (state) {
	case U_PA_READY: return "U_PA_READY";
	case U_RT_WAIT_LEFT: return "U_RT_WAIT_LEFT";
	case U_RT_PREDICTED: return "U_RT_PREDICTED";
	case U_RT_BEGUN: return "U_RT_BEGUN";
	case U_RT_DELIVERED: return "U_RT_DELIVERED";
	case U_RT_GPU_DONE: return "U_RT_GPU_DONE";
	default: return "UNKNOWN";
	}
}

static inline const char *
point_to_str(enum u_timing_point point)
{
	switch (point) {
	case U_TIMING_POINT_WAKE_UP: return "U_TIMING_POINT_WAKE_UP";
	case U_TIMING_POINT_BEGIN: return "U_TIMING_POINT_BEGIN";
	case U_TIMING_POINT_SUBMIT: return "U_TIMING_POINT_SUBMIT";
	default: return "UNKNOWN";
	}
}

#define DEBUG_PRINT_ID(ID) UPA_LOG_T("%" PRIi64, ID)
#define DEBUG_PRINT_ID_FRAME(ID, F) UPA_LOG_T("%" PRIi64 " (%" PRIi64 ", %s)", ID, F->frame_id, state_to_str(F->state))
#define DEBUG_PRINT_ID_FRAME_POINT(ID, F, P)                                                                           \
	UPA_LOG_T("%" PRIi64 " (%" PRIi64 ", %s) %s", frame_id, F->frame_id, state_to_str(F->state), point_to_str(P));

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
min_app_time(const struct pacing_app *pa)
{
	return (uint64_t)(pa->min_app_time_ms.val * (double)U_TIME_1MS_IN_NS);
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
	uint64_t total_ns = pa->app.cpu_time_ns + pa->app.draw_time_ns + pa->app.wait_time_ns;
	uint64_t min_ns = min_app_time(pa);

	if (total_ns < min_ns) {
		total_ns = min_ns;
	}

	return total_ns;
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
 * Metrics and tracing.
 *
 */

static void
do_metrics(struct pacing_app *pa, struct u_pa_frame *f, bool discarded)
{
	if (!u_metrics_is_active()) {
		return;
	}

	struct u_metrics_session_frame umsf = {
	    .session_id = pa->session_id,
	    .frame_id = f->frame_id,
	    .predicted_frame_time_ns = f->predicted_frame_time_ns,
	    .predicted_wake_up_time_ns = f->predicted_wake_up_time_ns,
	    .predicted_gpu_done_time_ns = f->predicted_gpu_done_time_ns,
	    .predicted_display_time_ns = f->predicted_display_time_ns,
	    .predicted_display_period_ns = f->predicted_display_period_ns,
	    .display_time_ns = f->display_time_ns,
	    .when_predicted_ns = f->when.predicted_ns,
	    .when_wait_woke_ns = f->when.wait_woke_ns,
	    .when_begin_ns = f->when.begin_ns,
	    .when_delivered_ns = f->when.delivered_ns,
	    .when_gpu_done_ns = f->when.gpu_done_ns,
	    .discarded = discarded,
	};

	u_metrics_write_session_frame(&umsf);
}

static void
do_tracing(struct pacing_app *pa, struct u_pa_frame *f)
{
	if (!U_TRACE_CATEGORY_IS_ENABLED(timing)) {
		return;
	}

#ifdef U_TRACE_TRACY // Uses Tracy specific things.
	uint64_t cpu_ns = f->when.begin_ns - f->when.wait_woke_ns;
	TracyCPlot("App CPU(ms)", time_ns_to_ms_f(cpu_ns));

	uint64_t draw_ns = f->when.delivered_ns - f->when.begin_ns;
	TracyCPlot("App Draw(ms)", time_ns_to_ms_f(draw_ns));

	uint64_t gpu_ns = f->when.gpu_done_ns - f->when.delivered_ns;
	TracyCPlot("App GPU(ms)", time_ns_to_ms_f(gpu_ns));

	uint64_t frame_ns = f->when.gpu_done_ns - f->when.wait_woke_ns;
	TracyCPlot("App Frame(ms)", time_ns_to_ms_f(frame_ns));

	int64_t wake_diff_ns = (int64_t)f->when.wait_woke_ns - (int64_t)f->predicted_wake_up_time_ns;
	TracyCPlot("App Wake Diff(ms)", time_ns_to_ms_f(wake_diff_ns));

	int64_t gpu_diff_ns = (int64_t)f->when.gpu_done_ns - (int64_t)f->predicted_gpu_done_time_ns;
	TracyCPlot("App Frame Diff(ms)", time_ns_to_ms_f(gpu_diff_ns));
#endif

#ifdef U_TRACE_PERCETTO // Uses Percetto specific things.
#define TE_BEG(TRACK, TIME, NAME) U_TRACE_EVENT_BEGIN_ON_TRACK_DATA(timing, TRACK, TIME, NAME, PERCETTO_I(f->frame_id))
#define TE_END(TRACK, TIME) U_TRACE_EVENT_END_ON_TRACK(timing, TRACK, TIME)

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

#undef TE_BEG
#undef TE_END
#endif
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

	DEBUG_PRINT_ID(frame_id);

	uint64_t period_ns = calc_period(pa);
	uint64_t predict_ns = predict_display_time(pa, now_ns, period_ns);
	// How long we think the frame should take.
	uint64_t frame_time_ns = total_app_time_ns(pa);
	// When should the client wake up.
	uint64_t wake_up_time_ns = predict_ns - total_app_and_compositor_time_ns(pa);
	// When the client's GPU work should have completed.
	uint64_t gpu_done_time_ns = predict_ns - total_compositor_time_ns(pa);

	pa->last_returned_ns = predict_ns;

	*out_wake_up_time = wake_up_time_ns;
	*out_predicted_display_time = predict_ns;
	*out_predicted_display_period = period_ns;

	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	struct u_pa_frame *f = &pa->frames[index];
	assert(f->frame_id == -1);
	assert(f->state == U_PA_READY);

	f->state = U_RT_PREDICTED;
	f->frame_id = frame_id;
	f->predicted_frame_time_ns = frame_time_ns;
	f->predicted_wake_up_time_ns = wake_up_time_ns;
	f->predicted_gpu_done_time_ns = gpu_done_time_ns;
	f->predicted_display_time_ns = predict_ns;
	f->predicted_display_period_ns = period_ns;
	f->when.predicted_ns = now_ns;

#ifdef U_TRACE_TRACY // Uses Tracy specific things.
	TracyCPlot("App time(ms)", time_ns_to_ms_f(total_app_time_ns(pa)));
#endif
}

static void
pa_mark_point(struct u_pacing_app *upa, int64_t frame_id, enum u_timing_point point, uint64_t when_ns)
{
	struct pacing_app *pa = pacing_app(upa);

	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	struct u_pa_frame *f = &pa->frames[index];

	DEBUG_PRINT_ID_FRAME_POINT(frame_id, f, point);

	assert(f->frame_id == frame_id);

	switch (point) {
	case U_TIMING_POINT_WAKE_UP:
		assert(f->state == U_RT_PREDICTED);

		f->when.wait_woke_ns = when_ns;
		f->state = U_RT_WAIT_LEFT;
		break;
	case U_TIMING_POINT_BEGIN:
		assert(f->state == U_RT_WAIT_LEFT);

		f->when.begin_ns = when_ns;
		f->state = U_RT_BEGUN;
		break;
	case U_TIMING_POINT_SUBMIT:
	default: assert(false);
	}
}

static void
pa_mark_discarded(struct u_pacing_app *upa, int64_t frame_id, uint64_t when_ns)
{
	struct pacing_app *pa = pacing_app(upa);

	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	struct u_pa_frame *f = &pa->frames[index];

	DEBUG_PRINT_ID_FRAME(frame_id, f);

	assert(f->frame_id == frame_id);
	assert(f->state == U_RT_WAIT_LEFT || f->state == U_RT_BEGUN);

	// Update all data.
	f->when.delivered_ns = when_ns;

	// Write out metrics data.
	do_metrics(pa, f, true);

	// Reset the frame.
	U_ZERO(f); // Zero for metrics
	f->state = U_PA_READY;
	f->frame_id = -1;
}

static void
pa_mark_delivered(struct u_pacing_app *upa, int64_t frame_id, uint64_t when_ns, uint64_t display_time_ns)
{
	struct pacing_app *pa = pacing_app(upa);

	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	struct u_pa_frame *f = &pa->frames[index];

	DEBUG_PRINT_ID_FRAME(frame_id, f);

	assert(f->frame_id == frame_id);
	assert(f->state == U_RT_BEGUN);

	// Update all data.
	f->when.delivered_ns = when_ns;
	f->display_time_ns = display_time_ns;
	f->state = U_RT_DELIVERED;
}

static void
pa_mark_gpu_done(struct u_pacing_app *upa, int64_t frame_id, uint64_t when_ns)
{
	struct pacing_app *pa = pacing_app(upa);

	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	struct u_pa_frame *f = &pa->frames[index];

	DEBUG_PRINT_ID_FRAME(frame_id, f);

	assert(f->frame_id == frame_id);
	assert(f->state == U_RT_DELIVERED);

	// Update all data.
	f->when.gpu_done_ns = when_ns;
	f->state = U_RT_GPU_DONE;


	/*
	 * Process data.
	 */

	int64_t diff_ns = f->predicted_gpu_done_time_ns - when_ns;
	bool late = false;
	if (diff_ns < 0) {
		diff_ns = -diff_ns;
		late = true;
	}

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

	// Write out metrics and tracing data.
	do_metrics(pa, f, false);
	do_tracing(pa, f);

#ifndef VALIDATE_LATCHED_AND_RETIRED
	// Reset the frame.
	U_ZERO(f); // Zero for metrics
	f->state = U_PA_READY;
	f->frame_id = -1;
#endif
}

static void
pa_latched(struct u_pacing_app *upa, int64_t frame_id, uint64_t when_ns, int64_t system_frame_id)
{
	struct pacing_app *pa = pacing_app(upa);

#ifdef VALIDATE_LATCHED_AND_RETIRED
	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	struct u_pa_frame *f = &pa->frames[index];
	assert(f->frame_id == frame_id);
	assert(f->state == U_RT_GPU_DONE);
#else
	(void)pa;
#endif

	struct u_metrics_used umu = {
	    .session_id = pa->session_id,
	    .session_frame_id = frame_id,
	    .system_frame_id = system_frame_id,
	    .when_ns = when_ns,
	};

	u_metrics_write_used(&umu);
}

static void
pa_retired(struct u_pacing_app *upa, int64_t frame_id, uint64_t when_ns)
{
	struct pacing_app *pa = pacing_app(upa);

#ifdef VALIDATE_LATCHED_AND_RETIRED
	size_t index = GET_INDEX_FROM_ID(pa, frame_id);
	struct u_pa_frame *f = &pa->frames[index];
	assert(f->frame_id == frame_id);
	assert(f->state == U_RT_GPU_DONE || f->state == U_RT_DELIVERED);

	// Reset the frame.
	U_ZERO(f); // Zero for metrics
	f->state = U_PA_READY;
	f->frame_id = -1;
#else
	(void)pa;
#endif
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
	u_var_remove_root(upa);

	free(upa);
}

static xrt_result_t
pa_create(int64_t session_id, struct u_pacing_app **out_upa)
{
	struct pacing_app *pa = U_TYPED_CALLOC(struct pacing_app);
	pa->base.predict = pa_predict;
	pa->base.mark_point = pa_mark_point;
	pa->base.mark_discarded = pa_mark_discarded;
	pa->base.mark_delivered = pa_mark_delivered;
	pa->base.mark_gpu_done = pa_mark_gpu_done;
	pa->base.latched = pa_latched;
	pa->base.retired = pa_retired;
	pa->base.info = pa_info;
	pa->base.destroy = pa_destroy;
	pa->session_id = session_id;
	pa->app.cpu_time_ns = U_TIME_1MS_IN_NS * 2;
	pa->app.draw_time_ns = U_TIME_1MS_IN_NS * 2;
	pa->app.margin_ns = U_TIME_1MS_IN_NS * 2;
	pa->min_app_time_ms = (struct u_var_draggable_f32){
	    .val = (float)debug_get_float_option_min_app_time_ms(),
	    .min = 1.0, // This can never be negative.
	    .step = 1.0,
	    .max = +120.0, // There are some really slow applications out there.
	};

	for (size_t i = 0; i < ARRAY_SIZE(pa->frames); i++) {
		pa->frames[i].state = U_PA_READY;
		pa->frames[i].frame_id = -1;
	}

	// U variable tracking.
	u_var_add_root(pa, "App timing info", true);
	u_var_add_draggable_f32(pa, &pa->min_app_time_ms, "Minimum app time(ms)");
	u_var_add_ro_u64(pa, &pa->app.cpu_time_ns, "CPU time(ns)");
	u_var_add_ro_u64(pa, &pa->app.draw_time_ns, "Draw time(ns)");
	u_var_add_ro_u64(pa, &pa->app.wait_time_ns, "GPU time(ns)");

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
	static int64_t session_id_gen = 0; // For now until global session id is introduced.

	return pa_create(session_id_gen++, out_upa);
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
