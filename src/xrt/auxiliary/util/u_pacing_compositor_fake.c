// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  For generating a fake timing.
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


/*
 *
 * Structs and defines.
 *
 */


struct fake_timing
{
	struct u_pacing_compositor base;

	/*!
	 * The periodicity of the display.
	 */
	uint64_t frame_period_ns;

	/*!
	 * When the last frame was presented, not displayed.
	 */
	uint64_t last_present_time_ns;

	/*!
	 * Very often the present time that we get from the system is only when
	 * the display engine starts scanning out from the buffers we provided,
	 * and not when the pixels turned into photons that the user sees.
	 */
	uint64_t present_to_display_offset_ns;

	// The amount of time that the application needs to render frame.
	uint64_t comp_time_ns;

	int64_t frame_id_generator;
};


/*
 *
 * Helper functions.
 *
 */

static inline struct fake_timing *
fake_timing(struct u_pacing_compositor *upc)
{
	return (struct fake_timing *)upc;
}

static uint64_t
predict_next_frame_present_time(struct fake_timing *ft, uint64_t now_ns)
{
	uint64_t time_needed_ns = ft->comp_time_ns;
	uint64_t predicted_present_time_ns = ft->last_present_time_ns + ft->frame_period_ns;

	while (now_ns + time_needed_ns > predicted_present_time_ns) {
		predicted_present_time_ns += ft->frame_period_ns;
	}

	return predicted_present_time_ns;
}

static uint64_t
get_percent_of_time(uint64_t time_ns, uint32_t fraction_percent)
{
	double fraction = (double)fraction_percent / 100.0;
	return time_s_to_ns(time_ns_to_s(time_ns) * fraction);
}


/*
 *
 * Member functions.
 *
 */

static void
pc_predict(struct u_pacing_compositor *upc,
           uint64_t now_ns,
           int64_t *out_frame_id,
           uint64_t *out_wake_up_time_ns,
           uint64_t *out_desired_present_time_ns,
           uint64_t *out_present_slop_ns,
           uint64_t *out_predicted_display_time_ns,
           uint64_t *out_predicted_display_period_ns,
           uint64_t *out_min_display_period_ns)
{
	struct fake_timing *ft = fake_timing(upc);

	int64_t frame_id = ft->frame_id_generator++;
	uint64_t desired_present_time_ns = predict_next_frame_present_time(ft, now_ns);
	uint64_t predicted_display_time_ns = desired_present_time_ns + ft->present_to_display_offset_ns;
	uint64_t wake_up_time_ns = desired_present_time_ns - ft->comp_time_ns;
	uint64_t present_slop_ns = U_TIME_HALF_MS_IN_NS;
	uint64_t predicted_display_period_ns = ft->frame_period_ns;
	uint64_t min_display_period_ns = ft->frame_period_ns;

	*out_frame_id = frame_id;
	*out_wake_up_time_ns = wake_up_time_ns;
	*out_desired_present_time_ns = desired_present_time_ns;
	*out_present_slop_ns = present_slop_ns;
	*out_predicted_display_time_ns = predicted_display_time_ns;
	*out_predicted_display_period_ns = predicted_display_period_ns;
	*out_min_display_period_ns = min_display_period_ns;
}

static void
pc_mark_point(struct u_pacing_compositor *upc, enum u_timing_point point, int64_t frame_id, uint64_t when_ns)
{
	// To help validate calling code.
	switch (point) {
	case U_TIMING_POINT_WAKE_UP: break;
	case U_TIMING_POINT_BEGIN: break;
	case U_TIMING_POINT_SUBMIT: break;
	default: assert(false);
	}
}

static void
pc_info(struct u_pacing_compositor *upc,
        int64_t frame_id,
        uint64_t desired_present_time_ns,
        uint64_t actual_present_time_ns,
        uint64_t earliest_present_time_ns,
        uint64_t present_margin_ns,
        uint64_t when_ns)
{
	/*
	 * The compositor might call this function because it selected the
	 * fake timing code even tho displaying timing is available.
	 */
}

static void
pc_update_vblank_from_display_control(struct u_pacing_compositor *upc, uint64_t last_vblank_ns)
{
	struct fake_timing *ft = fake_timing(upc);

	// Use the last vblank time to sync to the output.
	ft->last_present_time_ns = last_vblank_ns;
}

static void
pc_update_present_offset(struct u_pacing_compositor *upc, int64_t frame_id, uint64_t present_to_display_offset_ns)
{
	struct fake_timing *ft = fake_timing(upc);

	// not associating with frame IDs right now.
	(void)frame_id;

	ft->present_to_display_offset_ns = present_to_display_offset_ns;
}

static void
pc_destroy(struct u_pacing_compositor *upc)
{
	struct fake_timing *ft = fake_timing(upc);
	free(ft);
}


/*
 *
 * 'Exported' functions.
 *
 */

xrt_result_t
u_pc_fake_create(uint64_t estimated_frame_period_ns, uint64_t now_ns, struct u_pacing_compositor **out_upc)
{
	struct fake_timing *ft = U_TYPED_CALLOC(struct fake_timing);
	ft->base.predict = pc_predict;
	ft->base.mark_point = pc_mark_point;
	ft->base.info = pc_info;
	ft->base.update_vblank_from_display_control = pc_update_vblank_from_display_control;
	ft->base.update_present_offset = pc_update_present_offset;
	ft->base.destroy = pc_destroy;
	ft->frame_period_ns = estimated_frame_period_ns;


	// To make sure the code can start from a non-zero frame id.
	ft->frame_id_generator = 5;

	// An arbitrary guess.
	ft->present_to_display_offset_ns = U_TIME_1MS_IN_NS * 4;

	// 20% of the frame time.
	ft->comp_time_ns = get_percent_of_time(estimated_frame_period_ns, 20);

	// Or at least 2ms.
	if (ft->comp_time_ns < U_TIME_1MS_IN_NS * 2) {
		ft->comp_time_ns = U_TIME_1MS_IN_NS * 2;
	}

	// Make the next present time be in the future.
	ft->last_present_time_ns = now_ns + U_TIME_1MS_IN_NS * 50;

	// Return value.
	*out_upc = &ft->base;

	U_LOG_I("Created fake timing");

	return XRT_SUCCESS;
}
