// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions related to field-of-view.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#include "math/m_mathinclude.h"
#include "math/m_api.h"
#include "util/u_debug.h"

#include <math.h>
#include <stdio.h>
#include <assert.h>


DEBUG_GET_ONCE_BOOL_OPTION(views, "MATH_DEBUG_VIEWS", false)

/*!
 * Perform some of the computations from
 * "Computing Half-Fields-Of-View from Simpler Display Models",
 * to solve for the half-angles for a triangle where we know the center and
 * total angle but not the "distance".
 *
 * In the diagram below, the top angle is theta_total, the length of the bottom
 * is w_total, and the distance between the vertical line and the left corner is
 * w_1.
 * out_theta_1 is the angle at the top of the left-most right triangle,
 * out_theta_2 is the angle at the top of the right-most right triangle,
 * and out_d is the length of that center vertical line, a logical "distance".
 *
 * Any outparams that are NULL will simply not be set.
 *
 * The triangle need not be symmetrical, despite how the diagram looks.
 *
 * ```
 *               theta_total
 *                    *
 *       theta_1 -> / |  \ <- theta_2
 *                 /  |   \
 *                /   |d   \
 *               /    |     \
 *              -------------
 *              [ w_1 ][ w_2 ]
 *
 *              [ --- w  --- ]
 * ```
 *
 * Distances are in arbitrary but consistent units. Angles are in radians.
 *
 * @return true if successful.
 */
static bool
math_solve_triangle(
    double w_total, double w_1, double theta_total, double *out_theta_1, double *out_theta_2, double *out_d)
{
	/* should have at least one out-variable */
	assert(out_theta_1 || out_theta_2 || out_d);
	const double w_2 = w_total - w_1;

	const double u = w_2 / w_1;
	const double v = tan(theta_total);

	/* Parts of the quadratic formula solution */
	const double b = u + 1.0;
	const double root = sqrt(b + 4 * u * v * v);
	const double two_a = 2 * v;

	/* The two possible solutions. */
	const double tan_theta_2_plus = (-b + root) / two_a;
	const double tan_theta_2_minus = (-b - root) / two_a;
	const double theta_2_plus = atan(tan_theta_2_plus);
	const double theta_2_minus = atan(tan_theta_2_minus);

	/* Pick the solution that is in the right range. */
	double tan_theta_2 = 0;
	double theta_2 = 0;
	if (theta_2_plus > 0.f && theta_2_plus < theta_total) {
		// OH_DEBUG(ohd, "Using the + solution to the quadratic.");
		tan_theta_2 = tan_theta_2_plus;
		theta_2 = theta_2_plus;
	} else if (theta_2_minus > 0.f && theta_2_minus < theta_total) {
		// OH_DEBUG(ohd, "Using the - solution to the quadratic.");
		tan_theta_2 = tan_theta_2_minus;
		theta_2 = theta_2_minus;
	} else {
		// OH_ERROR(ohd, "NEITHER QUADRATIC SOLUTION APPLIES!");
		return false;
	}
#define METERS_FORMAT "%0.4fm"
#define DEG_FORMAT "%0.1f deg"
	if (debug_get_bool_option_views()) {
		const double rad_to_deg = M_1_PI * 180.0;
		// comments are to force wrapping
		U_LOG_D("w=" METERS_FORMAT " theta=" DEG_FORMAT "    w1=" METERS_FORMAT " theta1=" DEG_FORMAT
		        "    w2=" METERS_FORMAT " theta2=" DEG_FORMAT "    d=" METERS_FORMAT,
		        w_total, theta_total * rad_to_deg,         //
		        w_1, (theta_total - theta_2) * rad_to_deg, //
		        w_2, theta_2 * rad_to_deg,                 //
		        w_2 / tan_theta_2);
	}
	if (out_theta_2) {
		*out_theta_2 = theta_2;
	}

	if (out_theta_1) {
		*out_theta_1 = theta_total - theta_2;
	}
	if (out_d) {
		*out_d = w_2 / tan_theta_2;
	}
	return true;
}

bool
math_compute_fovs(double w_total,
                  double w_1,
                  double horizfov_total,
                  double h_total,
                  double h_1,
                  double vertfov_total,
                  struct xrt_fov *fov)
{
	double d = 0;
	double theta_1 = 0;
	double theta_2 = 0;
	if (!math_solve_triangle(w_total, w_1, horizfov_total, &theta_1, &theta_2, &d)) {
		/* failure is contagious */
		return false;
	}

	fov->angle_left = -theta_1;
	fov->angle_right = theta_2;

	double phi_1 = 0;
	double phi_2 = 0;
	if (vertfov_total == 0) {
		phi_1 = atan(h_1 / d);

		/* h_2 is "up".
		 * so the corresponding phi_2 is naturally positive.
		 */
		const double h_2 = h_total - h_1;
		phi_2 = atan(h_2 / d);
	} else {
		/* Run the same algorithm again for vertical. */
		if (!math_solve_triangle(h_total, h_1, vertfov_total, &phi_1, &phi_2, NULL)) {
			/* failure is contagious */
			return false;
		}
	}

	/* phi_1 is "down" so we record this as negative. */
	fov->angle_down = phi_1 * -1.0;
	fov->angle_up = phi_2;

	return true;
}
