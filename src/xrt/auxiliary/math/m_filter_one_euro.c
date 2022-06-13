// Copyright 2021, Collabora, Ltd.
// Copyright 2021, Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  The "One Euro Filter" for filtering interaction data.
 * @author Moses Turner <moses@collabora.com>
 * @author Jan Schmidt <jan@centricular.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 *
 * Based in part on https://github.com/thaytan/OpenHMD/blob/rift-kalman-filter/src/exponential-filter.c
 */


#include "m_filter_one_euro.h"

#include "math/m_mathinclude.h"

#include "math/m_vec2.h"
#include "math/m_vec3.h"
#include "util/u_time.h"
#include "util/u_misc.h"

static double
calc_smoothing_alpha(double Fc, double dt)
{
	/* Calculate alpha = (1 / (1 + tau/dt)) where tau = 1.0 / (2 * pi * Fc),
	 * this is a straight rearrangement with fewer divisions */
	double r = 2.0 * M_PI * Fc * dt;
	return r / (r + 1.0);
}

static double
exp_smooth(double alpha, float y, float prev_y)
{
	return alpha * y + (1.0 - alpha) * prev_y;
}

static struct xrt_vec2
exp_smooth_vec2(double alpha, struct xrt_vec2 y, struct xrt_vec2 prev_y)
{
	struct xrt_vec2 scaled_prev = m_vec2_mul_scalar(prev_y, 1.0 - alpha);
	struct xrt_vec2 scaled_new = m_vec2_mul_scalar(y, alpha);
	return m_vec2_add(scaled_prev, scaled_new);
}

static struct xrt_vec3
exp_smooth_vec3(double alpha, struct xrt_vec3 y, struct xrt_vec3 prev_y)
{
	struct xrt_vec3 scaled_prev = m_vec3_mul_scalar(prev_y, 1.0 - alpha);
	struct xrt_vec3 scaled_new = m_vec3_mul_scalar(y, alpha);
	return m_vec3_add(scaled_prev, scaled_new);
}

static inline struct xrt_quat
exp_smooth_quat(double alpha, struct xrt_quat y, struct xrt_quat prev_y)
{
	struct xrt_quat result;
	math_quat_slerp(&prev_y, &y, alpha, &result);
	return result;
}

static void
filter_one_euro_init(struct m_filter_one_euro_base *f, double fc_min, double fc_min_d, double beta)
{
	f->fc_min = fc_min;
	f->beta = beta;
	f->fc_min_d = fc_min_d;

	f->have_prev_y = false;
}

/// Is this the first sample? If so, please set up the common things.
static bool
filter_one_euro_handle_first_sample(struct m_filter_one_euro_base *f, uint64_t ts, bool commit)
{

	if (!f->have_prev_y) {
		/* First sample - no filtering yet */
		if (commit) {
			f->prev_ts = ts;
			f->have_prev_y = true;
		}
		return true;
	}
	return false;
}

/**
 * @brief Computes and outputs dt, updates the timestamp in the main structure if @p
 * commit is true, and computes and returns alpha_d
 *
 * @param[in,out] f filter base structure
 * @param[out] outDt pointer where we should put dt (time since last sample) after computing it.
 * @param ts data timestamp
 * @param commit true to commit changes to the filter state
 * @return alpha_d for filtering derivative
 */
static double
filter_one_euro_compute_alpha_d(struct m_filter_one_euro_base *f, double *outDt, uint64_t ts, bool commit)
{
	double dt = (double)(ts - f->prev_ts) / U_TIME_1S_IN_NS;
	if (commit) {
		f->prev_ts = ts;
	}
	*outDt = dt;
	return calc_smoothing_alpha(f->fc_min_d, dt);
}

/**
 * @brief Computes and returns alpha
 *
 * @param[in] f filter base structure
 * @param dt Time change in seconds
 * @param smoothed_derivative_magnitude the magnitude of the smoothed derivative
 * @return alpha for filtering derivative
 */
static double
filter_one_euro_compute_alpha(const struct m_filter_one_euro_base *f, double dt, double smoothed_derivative_magnitude)
{
	double fc_cutoff = f->fc_min + f->beta * smoothed_derivative_magnitude;
	return calc_smoothing_alpha(fc_cutoff, dt);
}


void
m_filter_euro_f32_init(struct m_filter_euro_f32 *f, double fc_min, double fc_min_d, double beta)
{
	filter_one_euro_init(&f->base, fc_min, fc_min_d, beta);
}

void
m_filter_f32_run(struct m_filter_euro_f32 *f, uint64_t ts, const float *in_y, float *out_y)
{

	if (filter_one_euro_handle_first_sample(&f->base, ts, true)) {
		/* First sample - no filtering yet */
		f->prev_dy = 0;
		f->prev_y = *in_y;

		*out_y = *in_y;
		return;
	}

	double dt = 0;
	double alpha_d = filter_one_euro_compute_alpha_d(&f->base, &dt, ts, true);

	double dy = (*in_y - f->prev_y) / dt;

	/* Smooth the dy values and use them to calculate the frequency cutoff for the main filter */
	f->prev_dy = exp_smooth(alpha_d, dy, f->prev_dy);

	double dy_mag = fabs(f->prev_dy);
	double alpha = filter_one_euro_compute_alpha(&f->base, dt, dy_mag);

	*out_y = f->prev_y = exp_smooth(alpha, *in_y, f->prev_y);
}

void
m_filter_euro_vec2_init(struct m_filter_euro_vec2 *f, double fc_min, double fc_min_d, double beta)
{
	filter_one_euro_init(&f->base, fc_min, fc_min_d, beta);
}

void
m_filter_euro_vec2_run(struct m_filter_euro_vec2 *f, uint64_t ts, const struct xrt_vec2 *in_y, struct xrt_vec2 *out_y)
{

	if (filter_one_euro_handle_first_sample(&f->base, ts, true)) {
		/* First sample - no filtering yet */
		U_ZERO(&f->prev_dy);
		f->prev_y = *in_y;
		*out_y = *in_y;
		return;
	}

	double dt = 0;
	double alpha_d = filter_one_euro_compute_alpha_d(&f->base, &dt, ts, true);

	struct xrt_vec2 dy = m_vec2_div_scalar(m_vec2_sub((*in_y), f->prev_y), dt);
	f->prev_dy = exp_smooth_vec2(alpha_d, dy, f->prev_dy);

	double dy_mag = m_vec2_len(f->prev_dy);
	double alpha = filter_one_euro_compute_alpha(&f->base, dt, dy_mag);

	/* Smooth the dy values and use them to calculate the frequency cutoff for the main filter */
	f->prev_y = exp_smooth_vec2(alpha, *in_y, f->prev_y);
	*out_y = f->prev_y;
}

void
m_filter_euro_vec2_run_no_commit(struct m_filter_euro_vec2 *f,
                                 uint64_t ts,
                                 const struct xrt_vec2 *in_y,
                                 struct xrt_vec2 *out_y)
{

	if (filter_one_euro_handle_first_sample(&f->base, ts, false)) {
		// First sample - no filtering yet - and we're not committing anything to the filter so return right
		// away
		*out_y = *in_y;
		return;
	}

	double dt = 0;
	double alpha_d = filter_one_euro_compute_alpha_d(&f->base, &dt, ts, false);

	struct xrt_vec2 dy = m_vec2_div_scalar(m_vec2_sub((*in_y), f->prev_y), dt);
	struct xrt_vec2 prev_dy = exp_smooth_vec2(alpha_d, dy, f->prev_dy);

	double dy_mag = m_vec2_len(prev_dy);

	/* Smooth the dy values and use them to calculate the frequency cutoff for the main filter */
	double alpha = filter_one_euro_compute_alpha(&f->base, dt, dy_mag);
	*out_y = exp_smooth_vec2(alpha, *in_y, f->prev_y);
}


void
m_filter_euro_vec3_init(struct m_filter_euro_vec3 *f, double fc_min, double fc_min_d, double beta)
{
	filter_one_euro_init(&f->base, fc_min, fc_min_d, beta);
}

void
m_filter_euro_vec3_run(struct m_filter_euro_vec3 *f, uint64_t ts, const struct xrt_vec3 *in_y, struct xrt_vec3 *out_y)
{
	if (filter_one_euro_handle_first_sample(&f->base, ts, true)) {
		/* First sample - no filtering yet */
		U_ZERO(&f->prev_dy);
		f->prev_y = *in_y;

		*out_y = *in_y;
		return;
	}

	double dt = 0;
	double alpha_d = filter_one_euro_compute_alpha_d(&f->base, &dt, ts, true);

	struct xrt_vec3 dy = m_vec3_div_scalar(m_vec3_sub((*in_y), f->prev_y), dt);
	f->prev_dy = exp_smooth_vec3(alpha_d, dy, f->prev_dy);

	double dy_mag = m_vec3_len(f->prev_dy);
	double alpha = filter_one_euro_compute_alpha(&f->base, dt, dy_mag);

	/* Smooth the dy values and use them to calculate the frequency cutoff for the main filter */
	f->prev_y = exp_smooth_vec3(alpha, *in_y, f->prev_y);
	*out_y = f->prev_y;
}

//! @todo fix order of args
void
m_filter_euro_quat_init(struct m_filter_euro_quat *f, double fc_min, double fc_min_d, double beta)
{
	filter_one_euro_init(&f->base, fc_min, fc_min_d, beta);
}

void
m_filter_euro_quat_run(struct m_filter_euro_quat *f, uint64_t ts, const struct xrt_quat *in_y, struct xrt_quat *out_y)
{
	if (filter_one_euro_handle_first_sample(&f->base, ts, true)) {
		/* First sample - no filtering yet */
		f->prev_dy = (struct xrt_quat)XRT_QUAT_IDENTITY;
		f->prev_y = *in_y;

		*out_y = *in_y;
		return;
	}

	double dt = 0;
	double alpha_d = filter_one_euro_compute_alpha_d(&f->base, &dt, ts, true);

	struct xrt_quat dy;
	math_quat_unrotate(&f->prev_y, in_y, &dy);

	// Scale dy with dt through a conversion to angle_axis
	struct xrt_vec3 dy_aa;
	math_quat_ln(&dy, &dy_aa);
	dy_aa = m_vec3_div_scalar(dy_aa, dt);
	math_quat_exp(&dy_aa, &dy);

	f->prev_dy = exp_smooth_quat(alpha_d, dy, f->prev_dy);

	// The magnitud of the smoothed dy (f->prev_dy) is its rotation angle in radians
	struct xrt_vec3 smooth_dy_aa;
	math_quat_ln(&f->prev_dy, &smooth_dy_aa);
	double smooth_dy_mag = m_vec3_len(smooth_dy_aa);

	double alpha = filter_one_euro_compute_alpha(&f->base, dt, smooth_dy_mag);

	/* Smooth the dy values and use them to calculate the frequency cutoff for the main filter */
	f->prev_y = exp_smooth_quat(alpha, *in_y, f->prev_y);
	*out_y = f->prev_y;
}
