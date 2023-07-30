// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Autodiff-safe rotations for Levenberg-Marquardt kinematic optimizer.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup tracking
 */


#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include "assert.h"
#include "float.h"
#include "lm_defines.hpp"

#ifdef XRT_OS_LINUX
#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)
#else
#define likely(x) x
#define unlikely(x) x
#endif

namespace xrt::tracking::hand::mercury::lm {

// For debugging.
#if 0
#include <iostream>
#define assert_quat_length_1(q)                                                                                        \
	{                                                                                                              \
		const T scale = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];                                 \
		if (abs(scale - T(1.0)) > 0.001) {                                                                     \
			std::cout << "Length bad! " << scale << std::endl;                                             \
			assert(false);                                                                                 \
		};                                                                                                     \
	}
#else
#define assert_quat_length_1(q)
#endif

#include "lm_rotations_ceres.inl"


template <typename T>
inline void
CurlToQuaternion(const T &curl, Quat<T> &result)
{
	const T theta_squared = curl * curl;

	// For points not at the origin, the full conversion is numerically stable.
	if (likely(theta_squared > T(0.0))) {
		const T theta = curl;
		const T half_theta = curl * T(0.5);
		const T k = sin(half_theta) / theta;
		result.w = cos(half_theta);
		result.x = curl * k;
		result.y = T(0.0);
		result.z = T(0.0);
	} else {
		// At the origin, dividing by 0 is probably bad. By approximating with a Taylor series,
		// and truncating at one term, the value and first derivatives will be
		// computed correctly when Jets are used.
		const T k(0.5);
		result.w = T(1.0);
		result.x = curl * k;
		result.y = T(0.0);
		result.z = T(0.0);
	}
}

template <typename T>
inline void
SwingToQuaternion(const Vec2<T> swing, Quat<T> &result)
{

	const T &a0 = swing.x;
	const T &a1 = swing.y;
	const T theta_squared = a0 * a0 + a1 * a1;

	// For points not at the origin, the full conversion is numerically stable.
	if (likely(theta_squared > T(0.0))) {
		const T theta = sqrt(theta_squared);
		const T half_theta = theta * T(0.5);
		const T k = sin(half_theta) / theta;
		result.w = cos(half_theta);
		result.x = a0 * k;
		result.y = a1 * k;
		result.z = T(0);
	} else {
		// At the origin, sqrt() will produce NaN in the derivative since
		// the argument is zero.  By approximating with a Taylor series,
		// and truncating at one term, the value and first derivatives will be
		// computed correctly when Jets are used.
		const T k(0.5);
		result.w = T(1.0);
		result.x = a0 * k;
		result.y = a1 * k;
		result.z = T(0);
	}
}


// See
// https://gitlab.freedesktop.org/slitcch/rotation_visualizer/-/blob/da5021d21600388b07c9c81000e866c4a2d015cb/lm_rotations_story.inl
// for the derivation
template <typename T>
inline void
SwingTwistToQuaternion(const Vec2<T> swing, const T twist, Quat<T> &result)
{

	T swing_x = swing.x;
	T swing_y = swing.y;

	T theta_squared_swing = swing_x * swing_x + swing_y * swing_y;

	// So it turns out that we don't get any divisions by zero or nans in the
	// differential part when twist is 0. I'm pretty sure we get lucky wrt. what cancels out

	if (theta_squared_swing > T(0.0)) {
		// theta_squared_swing is nonzero, so we the regular derived conversion.

		T theta = sqrt(theta_squared_swing);

		T half_theta = theta * T(0.5);

		// the "other" theta
		T half_twist = twist * T(0.5);

		T cos_half_theta = cos(half_theta);
		T cos_half_twist = cos(half_twist);

		T sin_half_twist = sin(half_twist);

		T sin_half_theta_over_theta = sin(half_theta) / theta;

		result.w = cos_half_theta * cos_half_twist;

		T x_part_1 = (swing_x * cos_half_twist * sin_half_theta_over_theta);
		T x_part_2 = (swing_y * sin_half_twist * sin_half_theta_over_theta);

		result.x = x_part_1 + x_part_2;

		T y_part_1 = (swing_y * cos_half_twist * sin_half_theta_over_theta);
		T y_part_2 = (swing_x * sin_half_twist * sin_half_theta_over_theta);

		result.y = y_part_1 - y_part_2;

		result.z = cos_half_theta * sin_half_twist;

	} else {
		// first: sin_half_theta/theta would be undefined, but
		// the limit approaches 0.5.

		// second: we only use theta to calculate sin_half_theta/theta
		// and that function's derivative at theta=0 is 0, so this formulation is fine.

		T half_twist = twist * T(0.5);

		T cos_half_twist = cos(half_twist);

		T sin_half_twist = sin(half_twist);

		T sin_half_theta_over_theta = T(0.5);

		// cos(0) is 1 so no cos_half_theta necessary
		result.w = cos_half_twist;

		T x_part_1 = (swing_x * cos_half_twist * sin_half_theta_over_theta);
		T x_part_2 = (swing_y * sin_half_twist * sin_half_theta_over_theta);

		result.x = x_part_1 + x_part_2;

		T y_part_1 = (swing_y * cos_half_twist * sin_half_theta_over_theta);
		T y_part_2 = (swing_x * sin_half_twist * sin_half_theta_over_theta);

		result.y = y_part_1 - y_part_2;

		result.z = sin_half_twist;
	}
}

} // namespace xrt::tracking::hand::mercury::lm
