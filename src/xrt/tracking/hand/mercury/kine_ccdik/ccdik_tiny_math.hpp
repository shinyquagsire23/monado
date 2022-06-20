// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Math for kinematic model
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */

#pragma once
#include "ccdik_defines.hpp"

namespace xrt::tracking::hand::mercury::ccdik {
// Waggle-curl-twist.
static inline void
wct_to_quat(wct_t wct, struct xrt_quat *out)
{
	XRT_TRACE_MARKER();
	xrt_vec3 waggle_axis = {0, 1, 0};
	xrt_quat just_waggle;
	math_quat_from_angle_vector(wct.waggle, &waggle_axis, &just_waggle);

	xrt_vec3 curl_axis = {1, 0, 0};
	xrt_quat just_curl;
	math_quat_from_angle_vector(wct.curl, &curl_axis, &just_curl);

	xrt_vec3 twist_axis = {0, 0, 1};
	xrt_quat just_twist;
	math_quat_from_angle_vector(wct.twist, &twist_axis, &just_twist);

	//! @optimize This should be a matrix multiplication...
	// Are you sure about that, previous moses? Pretty sure that quat products are faster than 3x3 matrix
	// products...
	*out = just_waggle;
	math_quat_rotate(out, &just_curl, out);
	math_quat_rotate(out, &just_twist, out);
}

// Inlines.
static inline float
rad(double degrees)
{
	return degrees * (M_PI / 180.0);
}

static inline void
clamp(float *in, float min, float max)
{
	*in = fminf(max, fmaxf(min, *in));
}

static inline void
clamp_to_r(float *in, float c, float r)
{
	clamp(in, c - r, c + r);
}
} // namespace xrt::tracking::hand::mercury::ccdik
