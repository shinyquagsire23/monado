// Copyright 2022, Campbell Suter
// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Test xrt_pose functions.
 * @author Campbell Suter <znix@znix.xyz>
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 */

#include "catch/catch.hpp"
#include "math/m_api.h"
#include "math/m_vec3.h"

TEST_CASE("Pose invert works")
{
	// Test that inverting a pose works correctly
	// Pick an arbitrary and non-trivial original pose
	struct xrt_pose orig = {};
	orig.position = {123.f, 456.f, 789.f};
	orig.orientation = {-0.439f, -0.561f, 0.072f, -0.698f};
	math_quat_normalize(&orig.orientation);

	// Invert it
	struct xrt_pose invert;
	math_pose_invert(&orig, &invert);

	// Multiply the poses together in both orders
	struct xrt_pose out_a;
	math_pose_transform(&orig, &invert, &out_a);
	struct xrt_pose out_b;
	math_pose_transform(&invert, &orig, &out_b);

	// A pose multiplied by it's inverse or vice-verse should have both a negligible rotation and position
	CHECK(m_vec3_len(out_a.position) < 0.001f);
	CHECK(1 - abs(out_a.orientation.w) < 0.001f);

	CHECK(m_vec3_len(out_b.position) < 0.001f);
	CHECK(1 - abs(out_b.orientation.w) < 0.001f);
}

TEST_CASE("Pose interpolation works")
{
	// A random pose
	struct xrt_vec3 pos_a = {1, 2, 3};
	struct xrt_quat ori_a = {1, 2, 3, 4};
	math_quat_normalize(&ori_a);
	struct xrt_pose a = {ori_a, pos_a};

	// The inverse of that pose
	struct xrt_vec3 pos_b = pos_a * -1;
	struct xrt_quat ori_b = {};
	math_quat_invert(&ori_a, &ori_b);
	struct xrt_pose b = {ori_b, pos_b};

	// The interpolation at 0.5 should be the identity
	struct xrt_pose res = {};
	math_pose_interpolate(&a, &b, 0.5, &res);

	constexpr float e = std::numeric_limits<float>::epsilon();
	CHECK(res.position.x == Approx(0).margin(e));
	CHECK(res.position.y == Approx(0).margin(e));
	CHECK(res.position.z == Approx(0).margin(e));
	CHECK(res.orientation.x == Approx(0).margin(e));
	CHECK(res.orientation.x == Approx(0).margin(e));
	CHECK(res.orientation.y == Approx(0).margin(e));
	CHECK(res.orientation.w == Approx(1).margin(e));
}
