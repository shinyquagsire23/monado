// Copyright 2022, Collabora, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Test for m_vec3_angle.
 * @author Moses Turner <moses@collabora.com>
 */
#include "xrt/xrt_defines.h"
#include <math/m_vec3.h>


#include "catch/catch.hpp"

TEST_CASE("Vec3Angle")
{
	float sqrt2_2 = sqrtf(2) / 2;
	CHECK(m_vec3_angle({1, 0, 0}, {-1, 0, 0}) == Approx(M_PI));
	CHECK(m_vec3_angle({1, 0, 0}, {0, 1, 0}) == Approx(M_PI / 2));
	CHECK(m_vec3_angle({1, 0, 0}, {sqrt2_2, sqrt2_2, 0}) == Approx(M_PI / 4));
}
