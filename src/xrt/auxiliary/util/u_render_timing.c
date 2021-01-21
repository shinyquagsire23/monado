// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared frame timing code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_time.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_logging.h"
#include "util/u_render_timing.h"

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>


/*
 *
 * Helpers
 *
 */

DEBUG_GET_ONCE_LOG_OPTION(ll, "U_RENDER_TIMING_LOG", U_LOGGING_WARN)

#define RT_LOG_T(...) U_LOG_IFL_T(debug_get_log_option_ll(), __VA_ARGS__)
#define RT_LOG_D(...) U_LOG_IFL_D(debug_get_log_option_ll(), __VA_ARGS__)
#define RT_LOG_I(...) U_LOG_IFL_I(debug_get_log_option_ll(), __VA_ARGS__)
#define RT_LOG_W(...) U_LOG_IFL_W(debug_get_log_option_ll(), __VA_ARGS__)
#define RT_LOG_E(...) U_LOG_IFL_E(debug_get_log_option_ll(), __VA_ARGS__)

#define DEBUG_PRINT_FRAME_ID() RT_LOG_T("%" PRIi64, frame_id)
#define GET_INDEX_FROM_ID(URTH, ID) ((uint64_t)(ID) % ARRAY_SIZE((URTH)->frames))

static uint64_t
min_period(const struct u_rt_helper *urth)
{
	return urth->last_input.predicted_display_period_ns;
}

static uint64_t
last_displayed(const struct u_rt_helper *urth)
{
	return urth->last_input.predicted_display_time_ns;
}

static uint64_t
get_last_input_plus_period_at_least_greater_then(struct u_rt_helper *urth, uint64_t then_ns)
{
	uint64_t val = last_displayed(urth);

	if (min_period(urth) == 0) {
		return then_ns;
	}

	while (val <= then_ns) {
		val += min_period(urth);
		assert(val != 0);
	}


	return val;
}


/*
 *
 * 'Exported' functions.
 *
 */

void
u_rt_helper_client_clear(struct u_rt_helper *urth)
{
	for (size_t i = 0; i < ARRAY_SIZE(urth->frames); i++) {
		urth->frames[i].state = U_RT_READY;
		urth->frames[i].frame_id = -1;
	}
}

void
u_rt_helper_init(struct u_rt_helper *urth)
{
	U_ZERO(urth);
	u_rt_helper_client_clear(urth);
}

void
u_rt_helper_predict(struct u_rt_helper *urth,
                    int64_t *out_frame_id,
                    uint64_t *predicted_display_time,
                    uint64_t *wake_up_time,
                    uint64_t *predicted_display_period,
                    uint64_t *min_display_period)
{
	int64_t frame_id = ++urth->frame_counter;
	*out_frame_id = frame_id;

	DEBUG_PRINT_FRAME_ID();

	uint64_t at_least_ns = os_monotonic_get_ns();

	// Don't return a time before the last returned type.
	if (at_least_ns < urth->last_returned_ns) {
		at_least_ns = urth->last_returned_ns;
	}

	uint64_t predict_ns = get_last_input_plus_period_at_least_greater_then(urth, at_least_ns);

	urth->last_returned_ns = predict_ns;

	*wake_up_time = predict_ns - min_period(urth);
	*predicted_display_time = predict_ns;
	*predicted_display_period = min_period(urth);
	*min_display_period = min_period(urth);

	size_t index = GET_INDEX_FROM_ID(urth, frame_id);
	assert(urth->frames[index].frame_id == -1);
	assert(urth->frames[index].state == U_RT_READY);

	/*
	 * When the client should deliver the frame to us, take into account the
	 * extra time needed by the main loop, plus a bit of extra time.
	 */
	uint64_t delivery_time_ns = predict_ns - urth->last_input.extra_ns - U_TIME_HALF_MS_IN_NS;

	urth->frames[index].when.predicted_ns = os_monotonic_get_ns();
	urth->frames[index].state = U_RT_PREDICTED;
	urth->frames[index].frame_id = frame_id;
	urth->frames[index].predicted_delivery_time_ns = delivery_time_ns;
}

void
u_rt_helper_mark_wait_woke(struct u_rt_helper *urth, int64_t frame_id)
{
	DEBUG_PRINT_FRAME_ID();

	size_t index = GET_INDEX_FROM_ID(urth, frame_id);
	assert(urth->frames[index].frame_id == frame_id);
	assert(urth->frames[index].state == U_RT_PREDICTED);

	urth->frames[index].when.wait_woke_ns = os_monotonic_get_ns();
	urth->frames[index].state = U_RT_WAIT_LEFT;
}

void
u_rt_helper_mark_begin(struct u_rt_helper *urth, int64_t frame_id)
{
	DEBUG_PRINT_FRAME_ID();

	size_t index = GET_INDEX_FROM_ID(urth, frame_id);
	assert(urth->frames[index].frame_id == frame_id);
	assert(urth->frames[index].state == U_RT_WAIT_LEFT);

	urth->frames[index].when.begin_ns = os_monotonic_get_ns();
	urth->frames[index].state = U_RT_BEGUN;
}

void
u_rt_helper_mark_discarded(struct u_rt_helper *urth, int64_t frame_id)
{
	DEBUG_PRINT_FRAME_ID();

	size_t index = GET_INDEX_FROM_ID(urth, frame_id);
	assert(urth->frames[index].frame_id == frame_id);
	assert(urth->frames[index].state == U_RT_WAIT_LEFT || urth->frames[index].state == U_RT_BEGUN);

	urth->frames[index].when.delivered_ns = os_monotonic_get_ns();
	urth->frames[index].state = U_RT_READY;
	urth->frames[index].frame_id = -1;
}

void
u_rt_helper_mark_delivered(struct u_rt_helper *urth, int64_t frame_id)
{
	DEBUG_PRINT_FRAME_ID();

	size_t index = GET_INDEX_FROM_ID(urth, frame_id);
	assert(urth->frames[index].frame_id == frame_id);
	assert(urth->frames[index].state == U_RT_BEGUN);

	uint64_t now_ns = os_monotonic_get_ns();

	urth->frames[index].when.delivered_ns = now_ns;
	urth->frames[index].state = U_RT_READY;
	urth->frames[index].frame_id = -1;

	int64_t diff_ns = urth->frames[index].predicted_delivery_time_ns - now_ns;
	bool late = false;
	if (diff_ns < 0) {
		diff_ns = -diff_ns;
		late = true;
	}

	int64_t ms100 = diff_ns / (1000 * 10);
	RT_LOG_D("Delivered frame %i.%02ims %s.", (int)ms100 / 100, (int)ms100 % 100, late ? "late" : "early");
}

void
u_rt_helper_new_sample(struct u_rt_helper *urth,
                       uint64_t predicted_display_time_ns,
                       uint64_t predicted_display_period_ns,
                       uint64_t extra_ns)
{
	urth->last_input.predicted_display_time_ns = predicted_display_time_ns;
	urth->last_input.predicted_display_period_ns = predicted_display_period_ns;
	urth->last_input.extra_ns = extra_ns;
}
