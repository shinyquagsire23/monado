// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Shared code for visualizing frametimes.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_defines.h"

#include "os/os_time.h"
#include "util/u_var.h"
#include "util/u_logging.h"
#include "util/u_misc.h"

#include <assert.h>

#define FPS_WIDGET_NUM_FRAME_TIMES 50

struct u_frame_times_widget
{
	//! Current Index for times_ns.
	int index;

	//! Timestamps of last-pushed frames.
	int64_t times_ns[FPS_WIDGET_NUM_FRAME_TIMES];

	//! Frametimes between last-pushed frames.
	float timings_ms[FPS_WIDGET_NUM_FRAME_TIMES];

	//! Average FPS of last NUM_FRAME_TIMES pushed frames.
	float fps;

	struct u_var_timing *debug_var;
};

static inline void
u_frame_times_widget_push_sample(struct u_frame_times_widget *widget, uint64_t new_frame_time)
{
	int last_index = widget->index;

	widget->index++;
	widget->index %= FPS_WIDGET_NUM_FRAME_TIMES;

	// update fps only once every FPS_NUM_TIMINGS
	if (widget->index == 0) {
		float total_s = 0;

		// frame *timings* are durations between *times*
		int NUM_FRAME_TIMINGS = FPS_WIDGET_NUM_FRAME_TIMES - 1;

		for (int i = 0; i < NUM_FRAME_TIMINGS; i++) {
			uint64_t frametime_ns = widget->times_ns[i + 1] - widget->times_ns[i];
			float frametime_s = (float)time_ns_to_s(frametime_ns);
			total_s += frametime_s;
		}
		float avg_frametime_s = total_s / ((float)NUM_FRAME_TIMINGS);
		widget->fps = 1.f / avg_frametime_s;
	}

	widget->times_ns[widget->index] = new_frame_time;

	assert(widget->index >= 0);
	assert(widget->index < FPS_WIDGET_NUM_FRAME_TIMES);
	assert(last_index >= 0);
	assert(last_index < FPS_WIDGET_NUM_FRAME_TIMES);

	uint64_t diff = widget->times_ns[widget->index] - widget->times_ns[last_index];
	widget->timings_ms[widget->index] = (float)time_ns_to_ms_f(diff);
}

static inline void
u_frame_times_widget_init(struct u_frame_times_widget *widget, float target_frame_time_ms, float range)
{
	uint64_t now = os_monotonic_get_ns();
	for (int i = 0; i < FPS_WIDGET_NUM_FRAME_TIMES; i++) {
		widget->times_ns[i] = now + i;
	}

	struct u_var_timing *ft = U_TYPED_CALLOC(struct u_var_timing);


	ft->values.data = widget->timings_ms;
	ft->values.length = FPS_WIDGET_NUM_FRAME_TIMES;
	ft->values.index_ptr = &widget->index;


	ft->reference_timing = target_frame_time_ms;
	ft->range = range;
	ft->unit = "ms";
	ft->dynamic_rescale = false;
	ft->center_reference_timing = true;
	widget->debug_var = ft;
	widget->index = 0;
}

// Call u_var_remove_root first!
static inline void
u_frame_times_widget_teardown(struct u_frame_times_widget *widget)
{
	free(widget->debug_var);
}
