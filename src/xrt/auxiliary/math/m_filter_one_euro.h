// Copyright 2021, Collabora, Ltd.
// Copyright 2021, Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking driver code.
 * @author Moses Turner <moses@collabora.com>
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_ht
 */

#pragma once

#include "xrt/xrt_defines.h"
#include "math/m_api.h"

// Suggestions. These are suitable for head tracking.
#define M_EURO_FILTER_HEAD_TRACKING_FCMIN 30.0
#define M_EURO_FILTER_HEAD_TRACKING_FCMIN_D 25.0
#define M_EURO_FILTER_HEAD_TRACKING_BETA 0.6

#ifdef __cplusplus
extern "C" {
#endif

struct m_filter_euro_f32
{
	/* Minimum frequency cutoff for filter and derivative respectively - default = 25.0 and 10.0 */
	float fc_min, fc_min_d;
	/* Beta value for "responsiveness" of filter - default = 0.01 */
	float beta;

	/* true if we have already processed a history sample */
	bool have_prev_y;

	/* Timestamp of previous sample (nanoseconds) and the sample */
	uint64_t prev_ts;
	double prev_y;
	double prev_dy;
};

struct m_filter_euro_vec2
{
	/* Minimum frequency cutoff for filter and derivative respectively - default = 25.0 and 10.0 */
	float fc_min, fc_min_d;
	/* Beta value for "responsiveness" of filter - default = 0.01 */
	float beta;

	/* true if we have already processed a history sample */
	bool have_prev_y;

	/* Timestamp of previous sample (nanoseconds) and the sample */
	uint64_t prev_ts;
	struct xrt_vec2 prev_y;
	struct xrt_vec2 prev_dy;
};

struct m_filter_euro_vec3
{
	/* Minimum frequency cutoff for filter and derivative respectively - default = 25.0 and 10.0 */
	float fc_min, fc_min_d;
	/* Beta value for "responsiveness" of filter - default = 0.01 */
	float beta;

	/* true if we have already processed a history sample */
	bool have_prev_y;

	/* Timestamp of previous sample (nanoseconds) and the sample */
	uint64_t prev_ts;
	struct xrt_vec3 prev_y;
	struct xrt_vec3 prev_dy;
};

void
m_filter_euro_f32_init(struct m_filter_euro_f32 *f, double fc_min, double beta, double fc_min_d);
void
m_filter_euro_f32_run(struct m_filter_euro_f32 *f, uint64_t ts, const float *in_y, float *out_y);

void
m_filter_euro_vec2_init(struct m_filter_euro_vec2 *f, double fc_min, double beta, double fc_min_d);
void
m_filter_euro_vec2_run(struct m_filter_euro_vec2 *f, uint64_t ts, const struct xrt_vec2 *in_y, struct xrt_vec2 *out_y);
void
m_filter_euro_vec2_run_no_commit(struct m_filter_euro_vec2 *f,
                                 uint64_t ts,
                                 const struct xrt_vec2 *in_y,
                                 struct xrt_vec2 *out_y);

void
m_filter_euro_vec3_init(struct m_filter_euro_vec3 *f, double fc_min, double beta, double fc_min_d);
void
m_filter_euro_vec3_run(struct m_filter_euro_vec3 *f, uint64_t ts, const struct xrt_vec3 *in_y, struct xrt_vec3 *out_y);

#ifdef __cplusplus
}
#endif
