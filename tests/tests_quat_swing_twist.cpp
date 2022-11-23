// Copyright 2022, Collabora, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Test for the quaternion swing-twist composition.
 * @author Moses Turner <moses@collabora.com>
 */
#include "math/m_api.h"
#include "xrt/xrt_defines.h"
#include <util/u_worker.hpp>
#include <util/u_logging.h>
#include <math/m_space.h>
#include <math/m_vec3.h>


#include "catch/catch.hpp"

#include <random>


#include "math/m_eigen_interop.hpp"
#include <Eigen/Core>

float
quat_difference(xrt_quat q1, xrt_quat q2)
{
	// https://math.stackexchange.com/a/90098
	// d(q1,q2)=1−⟨q1,q2⟩2

	float inner_product = (q1.w * q2.w) + (q1.x * q2.x) + (q1.y * q2.y) + (q1.z * q2.z);
	return 1.0 - (inner_product * inner_product);
}

TEST_CASE("SwingTwistTriviallyInvertibleIn180DegreeHemisphere")

{
	std::random_device dev;

	auto mt = std::mt19937(dev());

	auto rd = std::uniform_real_distribution<float>(-M_PI / 2, M_PI / 2);

	for (int i = 0; i < 20; i++) {
		xrt_vec2 swing = {rd(mt), rd(mt)};
		float twist = rd(mt);

		xrt_quat combquat;

		math_quat_from_swing_twist(&swing, twist, &combquat);

		xrt_vec2 recovered_swing;
		float recovered_twist;

		math_quat_to_swing_twist(&combquat, &recovered_swing, &recovered_twist);

		bool success = (fabsf(swing.x - recovered_swing.x) <= 0.001) &&
		               (fabsf(swing.y - recovered_swing.y) <= 0.001) &&
		               (fabsf(twist - recovered_twist) <= 0.001);

		if (!success) {
			U_LOG_E("Fail! Used swing %f %f, twist %f", swing.x, swing.y, twist);
		}
		CHECK(success);
	}
}


TEST_CASE("SwingTwistAlwaysInvertibleIfYouUseSoundRotationEqualities")

{
	std::random_device dev;

	auto mt = std::mt19937(dev());

	auto rd = std::uniform_real_distribution<float>(-1000, 1000);

	for (int i = 0; i < 20; i++) {

		xrt_vec2 swing = {rd(mt), rd(mt)};
		float twist = rd(mt);

		xrt_quat combquat;

		math_quat_from_swing_twist(&swing, twist, &combquat);

		xrt_vec2 recovered_swing;
		float recovered_twist;

		math_quat_to_swing_twist(&combquat, &recovered_swing, &recovered_twist);

		xrt_quat recovered_quat;

		math_quat_from_swing_twist(&recovered_swing, recovered_twist, &recovered_quat);

		bool success = quat_difference(combquat, recovered_quat) <= 0.001;

		if (!success) {
			// Test failed
			U_LOG_E("Fail! Used swing %f %f, twist %f", swing.x, swing.y, twist);
		}
		CHECK(success);
	}
}
