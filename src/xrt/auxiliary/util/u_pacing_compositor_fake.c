// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  For generating a fake timing.
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


/*
 *
 * Structs and defines.
 *
 */

DEBUG_GET_ONCE_FLOAT_OPTION(present_to_display_offset_ms, "U_PACING_COMP_PRESENT_TO_DISPLAY_OFFSET_MS", 4.0f)
DEBUG_GET_ONCE_FLOAT_OPTION(min_comp_time_ms, "U_PACING_COMP_MIN_TIME_MS", 3.0f)

/*!
 * A very simple pacer that tries it best to pace a compositor. Used when the
 * compositor can't get any good or limited feedback from the presentation
 * engine about timing.
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
	struct u_var_draggable_f32 present_to_display_offset_ms;

	//! The amount of time that the application needs to render frame.
	uint64_t comp_time_ns;

	//! This won't run out, trust me.
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
calc_display_time(struct fake_timing *ft, uint64_t present_time_ns)
{
	double offset_ms = ft->present_to_display_offset_ms.val;
	uint64_t offset_ns = time_ms_f_to_ns(offset_ms);
	return present_time_ns + offset_ns;
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
	uint64_t predicted_display_time_ns = calc_display_time(ft, desired_present_time_ns);

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

	if (!u_metrics_is_active()) {
		return;
	}

	struct u_metrics_system_frame umsf = {
	    .frame_id = frame_id,
	    .predicted_display_time_ns = predicted_display_time_ns,
	    .predicted_display_period_ns = predicted_display_period_ns,
	    .desired_present_time_ns = desired_present_time_ns,
	    .wake_up_time_ns = wake_up_time_ns,
	    .present_slop_ns = present_slop_ns,
	};

	u_metrics_write_system_frame(&umsf);
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
pc_info_gpu(
    struct u_pacing_compositor *upc, int64_t frame_id, uint64_t gpu_start_ns, uint64_t gpu_end_ns, uint64_t when_ns)
{
	if (u_metrics_is_active()) {
		struct u_metrics_system_gpu_info umgi = {
		    .frame_id = frame_id,
		    .gpu_start_ns = gpu_start_ns,
		    .gpu_end_ns = gpu_end_ns,
		    .when_ns = when_ns,
		};

		u_metrics_write_system_gpu_info(&umgi);
	}

#ifdef U_TRACE_PERCETTO // Uses Percetto specific things.
	if (U_TRACE_CATEGORY_IS_ENABLED(timing)) {
#define TE_BEG(TRACK, TIME, NAME) U_TRACE_EVENT_BEGIN_ON_TRACK_DATA(timing, TRACK, TIME, NAME, PERCETTO_I(frame_id))
#define TE_END(TRACK, TIME) U_TRACE_EVENT_END_ON_TRACK(timing, TRACK, TIME)

		TE_BEG(pc_gpu, gpu_start_ns, "gpu");
		TE_END(pc_gpu, gpu_end_ns);

#undef TE_BEG
#undef TE_END
	}
#endif

#ifdef U_TRACE_TRACY
	uint64_t diff_ns = gpu_end_ns - gpu_start_ns;
	TracyCPlot("Compositor GPU(ms)", time_ns_to_ms_f(diff_ns));
#endif
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

	double offset_ms = time_ns_to_ms_f(present_to_display_offset_ns);

	ft->present_to_display_offset_ms.val = offset_ms;
}

static void
pc_destroy(struct u_pacing_compositor *upc)
{
	struct fake_timing *ft = fake_timing(upc);

	u_var_remove_root(ft);

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
	ft->base.info_gpu = pc_info_gpu;
	ft->base.update_vblank_from_display_control = pc_update_vblank_from_display_control;
	ft->base.update_present_offset = pc_update_present_offset;
	ft->base.destroy = pc_destroy;
	ft->frame_period_ns = estimated_frame_period_ns;


	// To make sure the code can start from a non-zero frame id.
	ft->frame_id_generator = 5;

	// An arbitrary guess, that happens to be based on Index.
	float present_to_display_offset_ms = debug_get_float_option_present_to_display_offset_ms();

	// Present to display offset, aka vblank to pixel turning into photons.
	ft->present_to_display_offset_ms = (struct u_var_draggable_f32){
	    .val = present_to_display_offset_ms,
	    .min = 1.0, // A lot of things assumes this is not negative.
	    .step = 0.1,
	    .max = +40.0,
	};

	// 20% of the frame time.
	ft->comp_time_ns = get_percent_of_time(estimated_frame_period_ns, 20);

	// Or at least a certain amount of time.
	float min_comp_time_ms_f = debug_get_float_option_min_comp_time_ms();
	uint64_t min_comp_time_ns = time_ms_f_to_ns(min_comp_time_ms_f);

	if (ft->comp_time_ns < min_comp_time_ns) {
		ft->comp_time_ns = min_comp_time_ns;
	}

	// Make the next present time be in the future.
	ft->last_present_time_ns = now_ns + U_TIME_1MS_IN_NS * 50;

	// U variable tracking.
	u_var_add_root(ft, "Compositor timing info", true);
	u_var_add_draggable_f32(ft, &ft->present_to_display_offset_ms, "Present to display offset(ms)");
	u_var_add_ro_u64(ft, &ft->frame_period_ns, "Frame period(ns)");
	u_var_add_ro_u64(ft, &ft->comp_time_ns, "Compositor time(ns)");
	u_var_add_ro_u64(ft, &ft->last_present_time_ns, "Last present time(ns)");

	// Return value.
	*out_upc = &ft->base;

	U_LOG_I("Created fake timing");

	return XRT_SUCCESS;
}
