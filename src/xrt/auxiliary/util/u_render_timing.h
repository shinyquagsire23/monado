// Copyright 2020, Collabora, Ltd.
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
	uint64_t predicted;
	uint64_t wait_woke;
	uint64_t begin;
	uint64_t end_frame;
	int64_t frame_id;
	enum u_rt_state state;
};

struct u_rt_helper
{
	struct u_rt_frame frames[2];
	uint32_t current_frame;
	uint32_t next_frame;

	int64_t frame_counter;

	uint64_t extra;
	uint64_t period;
	uint64_t last_input;
	uint64_t last_returned;
};

void
u_rt_helper_init(struct u_rt_helper *urth);

void
u_rt_helper_clear(struct u_rt_helper *urth);

void
u_rt_helper_predict(struct u_rt_helper *urth,
                    int64_t *out_frame_id,
                    uint64_t *out_predicted_display_time,
                    uint64_t *out_wake_up_time,
                    uint64_t *out_predicted_display_period,
                    uint64_t *out_min_display_period);

void
u_rt_helper_mark_wait_woke(struct u_rt_helper *urth, int64_t frame_id);

void
u_rt_helper_mark_begin(struct u_rt_helper *urth, int64_t frame_id);

void
u_rt_helper_mark_discarded(struct u_rt_helper *urth, int64_t frame_id);

void
u_rt_helper_mark_delivered(struct u_rt_helper *urth, int64_t frame_id);

void
u_rt_helper_new_sample(struct u_rt_helper *urth,
                       uint64_t predict,
                       uint64_t extra,
                       uint64_t min_period);


#ifdef __cplusplus
}
#endif
