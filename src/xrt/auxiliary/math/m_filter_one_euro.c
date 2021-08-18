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

// https://github.com/thaytan/OpenHMD/blob/rift-kalman-filter/src/exponential-filter.c


#include "m_filter_one_euro.h"
#include "math/m_vec2.h"
#include "math/m_vec3.h"
#include "util/u_time.h"
#include "math/m_mathinclude.h"
#include "stdio.h"

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

void
m_filter_euro_f32_init(struct m_filter_euro_f32 *f, double fc_min, double beta, double fc_min_d)
{
	f->fc_min = fc_min;
	f->beta = beta;
	f->fc_min_d = fc_min_d;

	f->have_prev_y = false;
}

void
m_filter_f32_run(struct m_filter_euro_f32 *f, uint64_t ts, const float *in_y, float *out_y)
{
	double dy, dt, alpha_d;

	if (!f->have_prev_y) {
		/* First sample - no filtering yet */
		f->prev_dy = 0;
		f->prev_ts = ts;
		f->prev_y = *in_y;
		f->have_prev_y = true;

		*out_y = *in_y;
		return;
	}

	dt = (double)(ts - f->prev_ts) / U_TIME_1S_IN_NS;
	f->prev_ts = ts;

	dy = *in_y - f->prev_y;

	alpha_d = calc_smoothing_alpha(f->fc_min_d, dt);

	/* Smooth the dy values and use them to calculate the frequency cutoff for the main filter */
	double abs_dy, alpha, fc_cutoff;

	f->prev_dy = exp_smooth(alpha_d, dy, f->prev_dy);
	abs_dy = fabs(f->prev_dy);

	fc_cutoff = f->fc_min + f->beta * abs_dy;
	alpha = calc_smoothing_alpha(fc_cutoff, dt);

	*out_y = f->prev_y = exp_smooth(alpha, *in_y, f->prev_y);
}

void
m_filter_euro_vec2_init(struct m_filter_euro_vec2 *f, double fc_min, double fc_min_d, double beta)
{
	f->fc_min = fc_min;
	f->beta = beta;
	f->fc_min_d = fc_min_d;

	f->have_prev_y = false;
}

void
m_filter_euro_vec2_run(struct m_filter_euro_vec2 *f, uint64_t ts, const struct xrt_vec2 *in_y, struct xrt_vec2 *out_y)
{
	double dt, alpha_d;

	if (!f->have_prev_y) {
		/* First sample - no filtering yet */
		f->prev_dy.x = 0.0f;
		f->prev_dy.y = 0.0f;
		f->prev_ts = ts;
		f->prev_y = *in_y;
		f->have_prev_y = true;

		*out_y = *in_y;
		return;
	}

	dt = (double)(ts - f->prev_ts) / U_TIME_1S_IN_NS;
	f->prev_ts = ts;

	struct xrt_vec2 dy = m_vec2_sub((*in_y), f->prev_y);

	alpha_d = calc_smoothing_alpha(f->fc_min_d, dt);

	/* Smooth the dy values and use them to calculate the frequency cutoff for the main filter */
	float *in_y_arr = (float *)in_y;
	float *out_y_arr = (float *)out_y;

	float *dy_arr = (float *)(&dy);

	float *prev_y_arr = (float *)(&f->prev_y);
	float *prev_dy_arr = (float *)(&f->prev_dy);

	for (int i = 0; i < 2; i++) {
		double abs_dy, alpha, fc_cutoff;

		prev_dy_arr[i] = exp_smooth(alpha_d, dy_arr[i], prev_dy_arr[i]);
		abs_dy = fabs(dy_arr[i]);

		fc_cutoff = f->fc_min + f->beta * abs_dy;
		alpha = calc_smoothing_alpha(fc_cutoff, dt);
		out_y_arr[i] = prev_y_arr[i] = exp_smooth(alpha, in_y_arr[i], prev_y_arr[i]);
	}
}

void
m_filter_euro_vec2_run_no_commit(struct m_filter_euro_vec2 *f,
                                 uint64_t ts,
                                 const struct xrt_vec2 *in_y,
                                 struct xrt_vec2 *out_y)
{
	double dt, alpha_d;

	if (!f->have_prev_y) {
		// First sample - no filtering yet - and we're not committing anything to the filter so just return
		*out_y = *in_y;
		return;
	}

	dt = (double)(ts - f->prev_ts) / U_TIME_1S_IN_NS;

	struct xrt_vec2 dy = m_vec2_sub((*in_y), f->prev_y);

	alpha_d = calc_smoothing_alpha(f->fc_min_d, dt);

	/* Smooth the dy values and use them to calculate the frequency cutoff for the main filter */
	float *in_y_arr = (float *)in_y;
	float *out_y_arr = (float *)out_y;

	float *dy_arr = (float *)(&dy);

	float *prev_y_arr = (float *)(&f->prev_y);
	float *prev_dy_arr = (float *)(&f->prev_dy);

	for (int i = 0; i < 2; i++) {
		double abs_dy, alpha, fc_cutoff;

		prev_dy_arr[i] = exp_smooth(alpha_d, dy_arr[i], prev_dy_arr[i]);
		abs_dy = fabs(dy_arr[i]);

		fc_cutoff = f->fc_min + f->beta * abs_dy;
		alpha = calc_smoothing_alpha(fc_cutoff, dt);
		out_y_arr[i] = prev_y_arr[i] = exp_smooth(alpha, in_y_arr[i], prev_y_arr[i]);
	}
}


void
m_filter_euro_vec3_init(struct m_filter_euro_vec3 *f, double fc_min, double beta, double fc_min_d)
{
	f->fc_min = fc_min;
	f->beta = beta;
	f->fc_min_d = fc_min_d;

	f->have_prev_y = false;
}

void
m_filter_euro_vec3_run(struct m_filter_euro_vec3 *f, uint64_t ts, const struct xrt_vec3 *in_y, struct xrt_vec3 *out_y)
{
	double dt, alpha_d;

	if (!f->have_prev_y) {
		/* First sample - no filtering yet */
		f->prev_dy.x = 0;
		f->prev_dy.x = 0;
		f->prev_dy.x = 0;
		f->prev_ts = ts;
		f->prev_y = *in_y;
		f->have_prev_y = true;

		*out_y = *in_y;
		return;
	}

	dt = (double)(ts - f->prev_ts) / U_TIME_1S_IN_NS;
	f->prev_ts = ts;

	struct xrt_vec3 dy = m_vec3_sub((*in_y), f->prev_y);

	alpha_d = calc_smoothing_alpha(f->fc_min_d, dt);

	/* Smooth the dy values and use them to calculate the frequency cutoff for the main filter */
	float *in_y_arr = (float *)in_y;
	float *out_y_arr = (float *)out_y;

	float *dy_arr = (float *)(&dy);

	float *prev_y_arr = (float *)(&f->prev_y);
	float *prev_dy_arr = (float *)(&f->prev_dy);

	for (int i = 0; i < 3; i++) {
		double abs_dy, alpha, fc_cutoff;

		prev_dy_arr[i] = exp_smooth(alpha_d, dy_arr[i], prev_dy_arr[i]);
		abs_dy = fabs(dy_arr[i]);

		fc_cutoff = f->fc_min + f->beta * abs_dy;
		alpha = calc_smoothing_alpha(fc_cutoff, dt);
		out_y_arr[i] = prev_y_arr[i] = exp_smooth(alpha, in_y_arr[i], prev_y_arr[i]);
	}
}
