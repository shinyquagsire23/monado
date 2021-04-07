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
#include "util/u_logging.h"
#include "util/u_timing.h"

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>


/*
 *
 * Structs enums, and defines.
 *
 */

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

struct render_timing
{
	struct u_render_timing base;

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


/*
 *
 * Helpers
 *
 */

static inline struct render_timing *
render_timing(struct u_render_timing *urt)
{
	return (struct render_timing *)urt;
}

DEBUG_GET_ONCE_LOG_OPTION(ll, "U_TIMING_RENDER_LOG", U_LOGGING_WARN)

#define RT_LOG_T(...) U_LOG_IFL_T(debug_get_log_option_ll(), __VA_ARGS__)
#define RT_LOG_D(...) U_LOG_IFL_D(debug_get_log_option_ll(), __VA_ARGS__)
#define RT_LOG_I(...) U_LOG_IFL_I(debug_get_log_option_ll(), __VA_ARGS__)
#define RT_LOG_W(...) U_LOG_IFL_W(debug_get_log_option_ll(), __VA_ARGS__)
#define RT_LOG_E(...) U_LOG_IFL_E(debug_get_log_option_ll(), __VA_ARGS__)

#define DEBUG_PRINT_FRAME_ID() RT_LOG_T("%" PRIi64, frame_id)
#define GET_INDEX_FROM_ID(RT, ID) ((uint64_t)(ID) % ARRAY_SIZE((RT)->frames))

#define IIR_ALPHA_LT 0.5
#define IIR_ALPHA_GT 0.99

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
min_period(const struct render_timing *rt)
{
	return rt->last_input.predicted_display_period_ns;
}

static uint64_t
last_sample_displayed(const struct render_timing *rt)
{
	return rt->last_input.predicted_display_time_ns;
}

static uint64_t
last_return_predicted_display(const struct render_timing *rt)
{
	return rt->last_returned_ns;
}

static uint64_t
total_app_time_ns(const struct render_timing *rt)
{
	return rt->app.cpu_time_ns + rt->app.draw_time_ns;
}

static uint64_t
total_compositor_time_ns(const struct render_timing *rt)
{
	return rt->app.margin_ns + rt->last_input.extra_ns;
}

static uint64_t
total_app_and_compositor_time_ns(const struct render_timing *rt)
{
	return total_app_time_ns(rt) + total_compositor_time_ns(rt);
}

static uint64_t
calc_period(const struct render_timing *rt)
{
	// Error checking.
	uint64_t base_period_ns = min_period(rt);
	if (base_period_ns == 0) {
		assert(false && "Have not yet received and samples from timing driver.");
		base_period_ns = U_TIME_1MS_IN_NS * 16; // Sure
	}

	// Calculate the using both values separately.
	uint64_t period_ns = base_period_ns;
	while (rt->app.cpu_time_ns > period_ns) {
		period_ns += base_period_ns;
	}

	while (rt->app.draw_time_ns > period_ns) {
		period_ns += base_period_ns;
	}

	return period_ns;
}

static uint64_t
predict_display_time(const struct render_timing *rt, uint64_t period_ns)
{
	// Now
	uint64_t now_ns = os_monotonic_get_ns();


	// Total app and compositor time to produce a frame
	uint64_t app_and_compositor_time_ns = total_app_and_compositor_time_ns(rt);

	// Start from the last time that the driver displayed something.
	uint64_t val = last_sample_displayed(rt);

	// Return a time after the last returned display time.
	while (val <= last_return_predicted_display(rt)) {
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
rt_predict(struct u_render_timing *urt,
           int64_t *out_frame_id,
           uint64_t *out_wake_up_time,
           uint64_t *out_predicted_display_time,
           uint64_t *out_predicted_display_period)
{
	struct render_timing *rt = render_timing(urt);

	int64_t frame_id = ++rt->frame_counter;
	*out_frame_id = frame_id;

	DEBUG_PRINT_FRAME_ID();

	uint64_t period_ns = calc_period(rt);
	uint64_t predict_ns = predict_display_time(rt, period_ns);

	rt->last_returned_ns = predict_ns;

	*out_wake_up_time = predict_ns - total_app_and_compositor_time_ns(rt);
	*out_predicted_display_time = predict_ns;
	*out_predicted_display_period = period_ns;

	size_t index = GET_INDEX_FROM_ID(rt, frame_id);
	assert(rt->frames[index].frame_id == -1);
	assert(rt->frames[index].state == U_RT_READY);

	// When the client should deliver the frame to us.
	uint64_t delivery_time_ns = predict_ns - total_compositor_time_ns(rt);

	rt->frames[index].when.predicted_ns = os_monotonic_get_ns();
	rt->frames[index].state = U_RT_PREDICTED;
	rt->frames[index].frame_id = frame_id;
	rt->frames[index].predicted_delivery_time_ns = delivery_time_ns;
}

static void
rt_mark_point(struct u_render_timing *urt, int64_t frame_id, enum u_timing_point point, uint64_t when_ns)
{
	struct render_timing *rt = render_timing(urt);

	DEBUG_PRINT_FRAME_ID();

	size_t index = GET_INDEX_FROM_ID(rt, frame_id);
	assert(rt->frames[index].frame_id == frame_id);

	switch (point) {
	case U_TIMING_POINT_WAKE_UP:
		assert(rt->frames[index].state == U_RT_PREDICTED);

		rt->frames[index].when.wait_woke_ns = when_ns;
		rt->frames[index].state = U_RT_WAIT_LEFT;
		break;
	case U_TIMING_POINT_BEGIN:
		assert(rt->frames[index].state == U_RT_WAIT_LEFT);

		rt->frames[index].when.begin_ns = os_monotonic_get_ns();
		rt->frames[index].state = U_RT_BEGUN;
		break;
	case U_TIMING_POINT_SUBMIT:
	default: assert(false);
	}
}

static void
rt_mark_discarded(struct u_render_timing *urt, int64_t frame_id)
{
	struct render_timing *rt = render_timing(urt);

	DEBUG_PRINT_FRAME_ID();

	size_t index = GET_INDEX_FROM_ID(rt, frame_id);
	assert(rt->frames[index].frame_id == frame_id);
	assert(rt->frames[index].state == U_RT_WAIT_LEFT || rt->frames[index].state == U_RT_BEGUN);

	rt->frames[index].when.delivered_ns = os_monotonic_get_ns();
	rt->frames[index].state = U_RT_READY;
	rt->frames[index].frame_id = -1;
}

static void
rt_mark_delivered(struct u_render_timing *urt, int64_t frame_id)
{
	struct render_timing *rt = render_timing(urt);

	DEBUG_PRINT_FRAME_ID();

	size_t index = GET_INDEX_FROM_ID(rt, frame_id);
	assert(rt->frames[index].frame_id == frame_id);
	assert(rt->frames[index].state == U_RT_BEGUN);

	uint64_t now_ns = os_monotonic_get_ns();

	rt->frames[index].when.delivered_ns = now_ns;
	rt->frames[index].state = U_RT_READY;
	rt->frames[index].frame_id = -1;

	int64_t diff_ns = rt->frames[index].predicted_delivery_time_ns - now_ns;
	bool late = false;
	if (diff_ns < 0) {
		diff_ns = -diff_ns;
		late = true;
	}

#define NS_TO_MS_F(ns) (time_ns_to_s(ns) * 1000.0)

	uint64_t diff_cpu_ns = rt->frames[index].when.begin_ns - rt->frames[index].when.wait_woke_ns;
	uint64_t diff_draw_ns = rt->frames[index].when.delivered_ns - rt->frames[index].when.begin_ns;

	RT_LOG_D("Delivered frame %.2fms %s.\n\tcpu  o: %.2f, n: %.2f\n\tdraw o: %.2f, n: %.2f", //
	         time_ns_to_ms_f(diff_ns), late ? "late" : "early",                              //
	         time_ns_to_ms_f(rt->app.cpu_time_ns),                                           //
	         time_ns_to_ms_f(diff_cpu_ns),                                                   //
	         time_ns_to_ms_f(rt->app.draw_time_ns),                                          //
	         time_ns_to_ms_f(diff_draw_ns));

	do_iir_filter(&rt->app.cpu_time_ns, IIR_ALPHA_LT, IIR_ALPHA_GT, diff_cpu_ns);
	do_iir_filter(&rt->app.draw_time_ns, IIR_ALPHA_LT, IIR_ALPHA_GT, diff_draw_ns);
}

static void
rt_info(struct u_render_timing *urt,
        uint64_t predicted_display_time_ns,
        uint64_t predicted_display_period_ns,
        uint64_t extra_ns)
{
	struct render_timing *rt = render_timing(urt);

	rt->last_input.predicted_display_time_ns = predicted_display_time_ns;
	rt->last_input.predicted_display_period_ns = predicted_display_period_ns;
	rt->last_input.extra_ns = extra_ns;
}

static void
rt_destroy(struct u_render_timing *urt)
{
	free(urt);
}


/*
 *
 * 'Exported' functions.
 *
 */

xrt_result_t
u_rt_create(struct u_render_timing **out_urt)
{
	struct render_timing *rt = U_TYPED_CALLOC(struct render_timing);
	rt->base.predict = rt_predict;
	rt->base.mark_point = rt_mark_point;
	rt->base.mark_discarded = rt_mark_discarded;
	rt->base.mark_delivered = rt_mark_delivered;
	rt->base.info = rt_info;
	rt->base.destroy = rt_destroy;
	rt->app.cpu_time_ns = U_TIME_1MS_IN_NS * 2;
	rt->app.draw_time_ns = U_TIME_1MS_IN_NS * 2;
	rt->app.margin_ns = U_TIME_1MS_IN_NS / 2;

	for (size_t i = 0; i < ARRAY_SIZE(rt->frames); i++) {
		rt->frames[i].state = U_RT_READY;
		rt->frames[i].frame_id = -1;
	}

	*out_urt = &rt->base;

	return XRT_SUCCESS;
}
