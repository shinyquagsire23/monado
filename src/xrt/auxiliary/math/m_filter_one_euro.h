// Copyright 2021, Collabora, Ltd.
// Copyright 2021, Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header for a "One Euro Filter" implementation.
 * @author Moses Turner <moses@collabora.com>
 * @author Jan Schmidt <jan@centricular.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 *
 * See the original publication:
 *
 * Casiez, G., Roussel, N., and Vogel, D. 2012. 1 € filter: a simple speed-based low-pass filter for noisy input in
 * interactive systems. In: Proceedings of the SIGCHI Conference on Human Factors in Computing Systems. Association for
 * Computing Machinery, New York, NY, USA, 2527–2530.
 *
 * Available at: https://hal.inria.fr/hal-00670496/document
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

/**
 * @brief Base data type for One Euro filter instances.
 */
struct m_filter_one_euro_base
{
	/** Minimum frequency cutoff for filter, default = 25.0 */
	float fc_min;

	/** Minimum frequency cutoff for derivative filter, default = 10.0 */
	float fc_min_d;

	/** Beta value for "responsiveness" of filter - default = 0.01 */
	float beta;

	/** true if we have already processed a history sample */
	bool have_prev_y;

	/** Timestamp of previous sample (nanoseconds) */
	uint64_t prev_ts;
};

struct m_filter_euro_f32
{
	/** Base/common data */
	struct m_filter_one_euro_base base;

	/** The previous sample */
	double prev_y;

	/** The previous sample derivative */
	double prev_dy;
};

struct m_filter_euro_vec2
{
	/** Base/common data */
	struct m_filter_one_euro_base base;

	/** The previous sample */
	struct xrt_vec2 prev_y;

	/** The previous sample derivative */
	struct xrt_vec2 prev_dy;
};

struct m_filter_euro_vec3
{
	/** Base/common data */
	struct m_filter_one_euro_base base;

	/** The previous sample */
	struct xrt_vec3 prev_y;

	/** The previous sample derivative */
	struct xrt_vec3 prev_dy;
};

struct m_filter_euro_quat
{
	/** Base/common data */
	struct m_filter_one_euro_base base;

	/** The previous sample */
	struct xrt_quat prev_y;

	/** The previous sample derivative */
	struct xrt_quat prev_dy;
};

void
m_filter_euro_f32_init(struct m_filter_euro_f32 *f, double fc_min, double fc_min_d, double beta);
void
m_filter_euro_f32_run(struct m_filter_euro_f32 *f, uint64_t ts, const float *in_y, float *out_y);

void
m_filter_euro_vec2_init(struct m_filter_euro_vec2 *f, double fc_min, double fc_min_d, double beta);
void
m_filter_euro_vec2_run(struct m_filter_euro_vec2 *f, uint64_t ts, const struct xrt_vec2 *in_y, struct xrt_vec2 *out_y);
void
m_filter_euro_vec2_run_no_commit(struct m_filter_euro_vec2 *f,
                                 uint64_t ts,
                                 const struct xrt_vec2 *in_y,
                                 struct xrt_vec2 *out_y);

void
m_filter_euro_vec3_init(struct m_filter_euro_vec3 *f, double fc_min, double fc_min_d, double beta);
void
m_filter_euro_vec3_run(struct m_filter_euro_vec3 *f, uint64_t ts, const struct xrt_vec3 *in_y, struct xrt_vec3 *out_y);

void
m_filter_euro_quat_init(struct m_filter_euro_quat *f, double fc_min, double fc_min_d, double beta);
void
m_filter_euro_quat_run(struct m_filter_euro_quat *f, uint64_t ts, const struct xrt_quat *in_y, struct xrt_quat *out_y);

#ifdef __cplusplus
}
#endif
