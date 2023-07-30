// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple, untemplated, C, float-only, camera (un)projection functions for various camera models.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_tracking
 *
 * Some notes:
 * These functions should return exactly the same values as basalt-headers, down to floating point bits.
 *
 * They were mainly written as an expedient way to stop depending on OpenCV-based (un)projection code in Monado's hand
 * tracking code, and to encourage compiler optimizations through inlining.
 *
 * Current users:
 *
 * * Mercury hand tracking
 */

#pragma once
#include "math/m_vec2.h"
#include "math/m_matrix_2x2.h"
#include "math/m_mathinclude.h"

#include "t_tracking.h"

#include <assert.h>

/*!
 * Floating point parameters for @ref T_DISTORTION_FISHEYE_KB4
 * @ingroup aux_tracking
 */
struct t_camera_calibration_kb4_params_float
{
	float k1, k2, k3, k4;
};

/*!
 * Floating point parameters for @ref T_DISTORTION_OPENCV_RT8, also including metric_radius.
 * @ingroup aux_tracking
 */
struct t_camera_calibration_rt8_params_float
{
	float k1, k2, p1, p2, k3, k4, k5, k6, metric_radius;
};

/*!
 * Floating point calibration data for a single calibrated camera.
 * @note This is basically @ref t_camera_calibration, just without some compatibility stuff and using single floats
 * instead of doubles.
 * @ingroup aux_tracking
 */
struct t_camera_model_params
{
	float fx, fy, cx, cy;
	union {
		struct t_camera_calibration_kb4_params_float fisheye;
		struct t_camera_calibration_rt8_params_float rt8;
	};
	// This model gets reinterpreted from values in the main t_camera_calibration struct to either
	// * T_DISTORTION_FISHEYE_KB4
	// * T_DISTORTION_OPENCV_RADTAN_8
	enum t_camera_distortion_model model;
};


const float SQRT_EPSILON = 0.00316; // sqrt(1e-05)

/*
 * Functions for @ref T_DISTORTION_FISHEYE_KB4 (un)projections
 */

static inline float
kb4_calc_r_theta(const struct t_camera_model_params *dist, //
                 const float theta,                        //
                 const float theta2)
{
	float r_theta = dist->fisheye.k4 * theta2;
	r_theta += dist->fisheye.k3;
	r_theta *= theta2;
	r_theta += dist->fisheye.k2;
	r_theta *= theta2;
	r_theta += dist->fisheye.k1;
	r_theta *= theta2;
	r_theta += 1.0f;
	r_theta *= theta;

	return r_theta;
}

static inline bool
kb4_project(const struct t_camera_model_params *dist, //
            const float x,                            //
            const float y,                            //
            const float z,                            //
            float *out_x,                             //
            float *out_y)
{
	bool is_valid = true;
	const float r2 = x * x + y * y;
	const float r = sqrtf(r2);

	if (r > SQRT_EPSILON) {


		const float theta = atan2(r, z);
		const float theta2 = theta * theta;

		float r_theta = kb4_calc_r_theta(dist, theta, theta2);

		const float mx = x * r_theta / r;
		const float my = y * r_theta / r;

		*out_x = dist->fx * mx + dist->cx;
		*out_y = dist->fy * my + dist->cy;
	} else {
		// Check that the point is not cloze to zero norm
		if (z < SQRT_EPSILON) {
			is_valid = false;
		}
		*out_x = dist->fx * x / z + dist->cx;
		*out_y = dist->fy * y / z + dist->cy;
	}

	return is_valid;
}

static inline float
kb4_solve_theta(const struct t_camera_model_params *dist, const float *r_theta, float *d_func_d_theta)
{
	float theta = *r_theta;
	for (int i = 4; i > 0; i--) {
		float theta2 = theta * theta;

		float func = dist->fisheye.k4 * theta2;
		func += dist->fisheye.k3;
		func *= theta2;
		func += dist->fisheye.k2;
		func *= theta2;
		func += dist->fisheye.k1;
		func *= theta2;
		func += 1.0f;
		func *= theta;

		*d_func_d_theta = 9.0f * dist->fisheye.k4 * theta2;
		*d_func_d_theta += 7.0f * dist->fisheye.k3;
		*d_func_d_theta *= theta2;
		*d_func_d_theta += 5.0f * dist->fisheye.k2;
		*d_func_d_theta *= theta2;
		*d_func_d_theta += 3.0f * dist->fisheye.k1;
		*d_func_d_theta *= theta2;
		*d_func_d_theta += 1.0f;

		// Iteration of Newton method
		theta += ((*r_theta) - func) / (*d_func_d_theta);
	}

	return theta;
}

static inline bool
kb4_unproject(const struct t_camera_model_params *dist, //
              const float x,                            //
              const float y,                            //
              float *out_x,                             //
              float *out_y,                             //
              float *out_z)
{
	const float mx = (x - dist->cx) / dist->fx;
	const float my = (y - dist->cy) / dist->fy;

	float theta(0);
	float sin_theta(0);
	float cos_theta(1);
	float thetad = sqrt(mx * mx + my * my);
	float scaling(1);
	float d_func_d_theta(0);

	if (thetad > SQRT_EPSILON) {
		theta = kb4_solve_theta(dist, &thetad, &d_func_d_theta);

		sin_theta = sin(theta);
		cos_theta = cos(theta);
		scaling = sin_theta / thetad;
	}

	*out_x = mx * scaling;
	*out_y = my * scaling;
	*out_z = cos_theta;

	//!@todo I'm not 100% sure if kb4 is always non-injective. basalt-headers always returns true here, so it might
	//! be wrong too.
	return true;
}

/*
 * Functions for radial-tangential (un)projections
 */

static inline bool
rt8_project(const struct t_camera_model_params *dist, //
            const float x,                            //
            const float y,                            //
            const float z,                            //
            float *out_x,                             //
            float *out_y)
{
	const float xp = x / z;
	const float yp = y / z;
	const float rp2 = xp * xp + yp * yp;
	const float cdist = (1.0f + rp2 * (dist->rt8.k1 + rp2 * (dist->rt8.k2 + rp2 * dist->rt8.k3))) /
	                    (1.0f + rp2 * (dist->rt8.k4 + rp2 * (dist->rt8.k5 + rp2 * dist->rt8.k6)));
	const float deltaX = 2.0f * dist->rt8.p1 * xp * yp + dist->rt8.p2 * (rp2 + 2.0f * xp * xp);
	const float deltaY = 2.0f * dist->rt8.p2 * xp * yp + dist->rt8.p1 * (rp2 + 2.0f * yp * yp);
	const float xpp = xp * cdist + deltaX;
	const float ypp = yp * cdist + deltaY;
	const float u = dist->fx * xpp + dist->cx;
	const float v = dist->fy * ypp + dist->cy;

	*out_x = u;
	*out_y = v;

	const float rpmax = dist->rt8.metric_radius;
	bool positive_z = z >= SQRT_EPSILON; // Sophus::Constants<Scalar>::epsilonSqrt();
	bool in_injective_area = rpmax == 0.0f ? true : rp2 <= rpmax * rpmax;
	bool is_valid = positive_z && in_injective_area;
	return is_valid;
}

static inline void
rt8_distort(const t_camera_model_params *params,
            const xrt_vec2 *undist,
            xrt_vec2 *dist,
            xrt_matrix_2x2 *d_dist_d_undist)
{
	const float &k1 = params->rt8.k1;
	const float &k2 = params->rt8.k2;
	const float &p1 = params->rt8.p1;
	const float &p2 = params->rt8.p2;
	const float &k3 = params->rt8.k3;
	const float &k4 = params->rt8.k4;
	const float &k5 = params->rt8.k5;
	const float &k6 = params->rt8.k6;

	const float xp = undist->x;
	const float yp = undist->y;
	const float rp2 = xp * xp + yp * yp;
	const float cdist = (1.0f + rp2 * (k1 + rp2 * (k2 + rp2 * k3))) / (1.0f + rp2 * (k4 + rp2 * (k5 + rp2 * k6)));
	const float deltaX = 2.0f * p1 * xp * yp + p2 * (rp2 + 2.0f * xp * xp);
	const float deltaY = 2.0f * p2 * xp * yp + p1 * (rp2 + 2.0f * yp * yp);
	const float xpp = xp * cdist + deltaX;
	const float ypp = yp * cdist + deltaY;
	dist->x = xpp;
	dist->y = ypp;

	// Jacobian part!
	// Expressions derived with sympy
	const float v0 = xp * xp;
	const float v1 = yp * yp;
	const float v2 = v0 + v1;
	const float v3 = k6 * v2;
	const float v4 = k4 + v2 * (k5 + v3);
	const float v5 = v2 * v4 + 1.0f;
	const float v6 = v5 * v5;
	const float v7 = 1.0f / v6;
	const float v8 = p1 * yp;
	const float v9 = p2 * xp;
	const float v10 = 2.0f * v6;
	const float v11 = k3 * v2;
	const float v12 = k1 + v2 * (k2 + v11);
	const float v13 = v12 * v2 + 1.0f;
	const float v14 = v13 * (v2 * (k5 + 2.0f * v3) + v4);
	const float v15 = 2.0f * v14;
	const float v16 = v12 + v2 * (k2 + 2.0f * v11);
	const float v17 = 2.0f * v16;
	const float v18 = xp * yp;
	const float v19 = 2.0f * v7 * (-v14 * v18 + v16 * v18 * v5 + v6 * (p1 * xp + p2 * yp));

	const float dxpp_dxp = v7 * (-v0 * v15 + v10 * (v8 + 3.0f * v9) + v5 * (v0 * v17 + v13));
	const float dxpp_dyp = v19;
	const float dypp_dxp = v19;
	const float dypp_dyp = v7 * (-v1 * v15 + v10 * (3.0f * v8 + v9) + v5 * (v1 * v17 + v13));

	d_dist_d_undist->v[0] = dxpp_dxp;
	d_dist_d_undist->v[1] = dxpp_dyp;
	d_dist_d_undist->v[2] = dypp_dxp;
	d_dist_d_undist->v[3] = dypp_dyp;
}

static inline bool
rt8_unproject(
    const struct t_camera_model_params *hg_dist, const float u, const float v, float *out_x, float *out_y, float *out_z)
{

	const float x0 = (u - hg_dist->cx) / hg_dist->fx;
	const float y0 = (v - hg_dist->cy) / hg_dist->fy;

	//! @todo Decide if besides rpmax, it could be useful to have an rppmax
	//! field. A good starting point to having this would be using the sqrt of
	//! the max rpp2 value computed in the optimization of `computeRpmax()`.

	// Newton solver
	struct xrt_vec2 dist = {x0, y0};
	struct xrt_vec2 undist = dist;

	const int N = 5; // Max iterations
	for (int i = 0; i < N; i++) {
		struct xrt_matrix_2x2 J;
		struct xrt_vec2 fundist;

		rt8_distort(hg_dist, &undist, &fundist, &J);
		struct xrt_vec2 residual = m_vec2_sub(fundist, dist);

		// fundist - dist;
		struct xrt_matrix_2x2 J_inverse;

		m_mat2x2_invert(&J, &J_inverse);

		struct xrt_vec2 undist_sub;

		m_mat2x2_transform_vec2(&J_inverse, &residual, &undist_sub);

		undist = m_vec2_sub(undist, undist_sub);
		if (m_vec2_len(residual) < SQRT_EPSILON) {
			break;
		}
	}
	const float xp = undist.x;
	const float yp = undist.y;


	const float norm_inv = 1.0f / sqrt(xp * xp + yp * yp + 1.0f);
	*out_x = xp * norm_inv;
	*out_y = yp * norm_inv;
	*out_z = norm_inv;



	const float rp2 = xp * xp + yp * yp;
	bool in_injective_area =
	    hg_dist->rt8.metric_radius == 0.0f ? true : rp2 <= hg_dist->rt8.metric_radius * hg_dist->rt8.metric_radius;
	bool is_valid = in_injective_area;

	return is_valid;
}

#if 0
static inline bool
zero_distortion_pinhole_project(const struct t_camera_model_params *dist, //
                                const float x,                             //
                                const float y,                             //
                                const float z,                             //
                                float *out_x,                              //
                                float *out_y)
{
	*out_x = ((dist->fx * x / z) + dist->cx);
	*out_y = ((dist->fy * y / z) + dist->cy);

	bool is_valid = z >= SQRT_EPSILON;
	return is_valid;
}
static inline bool
zero_distortion_pinhole_unproject(const struct t_camera_model_params *dist, //
                                  const float x,                             //
                                  const float y,                             //
                                  float *out_x,                              //
                                  float *out_y,                              //
                                  float *out_z)
{
	const float mx = (x - dist->cx) / dist->fx;
	const float my = (y - dist->cy) / dist->fy;

	const float r2 = mx * mx + my * my;

	const float norm = sqrtf(1.0f + r2);

	const float norm_inv = 1.0f / norm;

	*out_x = mx * norm_inv;
	*out_y = my * norm_inv;
	*out_z = norm_inv;

	// Pinhole unprojection is always valid :)
	return true;
}
#endif

/*
 * Misc functions.
 */

static inline void
interpret_as_rt8(const struct t_camera_calibration *cc, struct t_camera_model_params *out_params)
{
	// Make a temporary buffer that definitely has zeros in it.
	double distortion_tmp[XRT_DISTORTION_MAX_DIM] = {0};

	if (cc->distortion_model != T_DISTORTION_OPENCV_RADTAN_8) {
		U_LOG_W("Reinterpreting %s distortion as %s", t_stringify_camera_distortion_model(cc->distortion_model),
		        t_stringify_camera_distortion_model(T_DISTORTION_OPENCV_RADTAN_8));
	}

	size_t dist_num = t_num_params_from_distortion_model(cc->distortion_model);

	for (size_t i = 0; i < dist_num; i++) {
		// Copy only the valid values over. The high indices will be zero, which means that rt4 and rt5
		// calibrations will work correctly.
		distortion_tmp[i] = cc->distortion_parameters_as_array[i];
	}

	int acc_idx = 0;
	out_params->rt8.k1 = distortion_tmp[acc_idx++];
	out_params->rt8.k2 = distortion_tmp[acc_idx++];
	out_params->rt8.p1 = distortion_tmp[acc_idx++];
	out_params->rt8.p2 = distortion_tmp[acc_idx++];
	out_params->rt8.k3 = distortion_tmp[acc_idx++];
	out_params->rt8.k4 = distortion_tmp[acc_idx++];
	out_params->rt8.k5 = distortion_tmp[acc_idx++];
	out_params->rt8.k6 = distortion_tmp[acc_idx++];



	if (cc->distortion_model == T_DISTORTION_WMR) {
		out_params->rt8.metric_radius = cc->wmr.rpmax;
	} else {
		out_params->rt8.metric_radius = 0;
	}
	out_params->model = T_DISTORTION_OPENCV_RADTAN_8;
}

/*
 * "Exported" functions.
 */

/*!
 * Takes a @ref t_camera_calibration through \p cc, and returns a @ref t_camera_model_params that shadows \p cc
 * 's parameters through \p out_params
 */
static inline void
t_camera_model_params_from_t_camera_calibration(const struct t_camera_calibration *cc,
                                                struct t_camera_model_params *out_params)
{
	// Paranoia.
	U_ZERO(out_params);

	// First row, first column.
	out_params->fx = (float)cc->intrinsics[0][0];
	// Second row, second column.
	out_params->fy = (float)cc->intrinsics[1][1];
	// First row, third column.
	out_params->cx = (float)cc->intrinsics[0][2];
	// Second row, third column.
	out_params->cy = (float)cc->intrinsics[1][2];

	out_params->model = cc->distortion_model;



	switch (cc->distortion_model) {
	case T_DISTORTION_FISHEYE_KB4: {
		out_params->fisheye.k1 = (float)cc->kb4.k1;
		out_params->fisheye.k2 = (float)cc->kb4.k2;
		out_params->fisheye.k3 = (float)cc->kb4.k3;
		out_params->fisheye.k4 = (float)cc->kb4.k4;
	} break;
	case T_DISTORTION_OPENCV_RADTAN_14:
	case T_DISTORTION_OPENCV_RADTAN_5:
	case T_DISTORTION_OPENCV_RADTAN_8:
	case T_DISTORTION_WMR: interpret_as_rt8(cc, out_params); break;
	default:
		U_LOG_E("t_camera_un_projections doesn't support camera model %s yet!",
		        t_stringify_camera_distortion_model(cc->distortion_model));
		break;
	}
}

/*!
 * Takes a 2D image-space point through \p x and \p y, unprojects it to a normalized 3D direction, and returns
 * the result through \p out_x, \p out_y and \p out_z
 */
static inline bool
t_camera_models_unproject(
    const struct t_camera_model_params *dist, const float x, const float y, float *out_x, float *out_y, float *out_z)
{
	switch (dist->model) {
	case T_DISTORTION_OPENCV_RADTAN_8: {
		return rt8_unproject(dist, x, y, out_x, out_y, out_z);
	}; break;
	case T_DISTORTION_FISHEYE_KB4: {
		return kb4_unproject(dist, x, y, out_x, out_y, out_z);
	}; break;
	// Return false so we don't get warnings on Release builds.
	default: assert(false); return false;
	}
}

/*!
 * Takes a 2D image-space point through \p x and \p y, unprojects it to a normalized 3D direction, flips its Y
 * and Z values (performing a coordinate space transform from +Z forward -Y up to -Z forward +Y up) and returns the
 * result through \p out_x, \p out_y and \p out_z
 */
static inline bool
t_camera_models_unproject_and_flip(
    const struct t_camera_model_params *dist, const float x, const float y, float *out_x, float *out_y, float *out_z)
{
	bool ret = t_camera_models_unproject(dist, x, y, out_x, out_y, out_z);

	*out_z *= -1;
	*out_y *= -1;
	return ret;
}


/*!
 * Takes a 3D point through \p x, \p y, and \p z, projects it into image space, and returns the result
 * through \p out_x and \p out_y
 */
static inline bool
t_camera_models_project(const struct t_camera_model_params *dist, //
                        const float x,                            //
                        const float y,                            //
                        const float z,                            //
                        float *out_x,                             //
                        float *out_y)
{
	switch (dist->model) {
	case T_DISTORTION_OPENCV_RADTAN_8: {
		return rt8_project(dist, x, y, z, out_x, out_y);
	}; break;
	case T_DISTORTION_FISHEYE_KB4: {
		return kb4_project(dist, x, y, z, out_x, out_y);
	}; break;
	// Return false so we don't get warnings on Release builds.
	default: assert(false); return false;
	}
}

/*!
 * Takes a 3D point through \p x, \p y, and \p z, flips its Y and Z values (performing a coordinate space
 * transform from -Z forward +Y up to +Z forward -Y up), projects it into image space, and returns the result through
 * \p out_x and \p out_y
 */
static inline bool
t_camera_models_flip_and_project(const struct t_camera_model_params *dist, //
                                 const float x,                            //
                                 const float y,                            //
                                 const float z,                            //
                                 float *out_x,                             //
                                 float *out_y)
{
	float _y = y * -1;
	float _z = z * -1;

	return t_camera_models_project(dist, x, _y, _z, out_x, out_y);
}
