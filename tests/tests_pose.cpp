// Copyright 2022, Campbell Suter
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Maths function tests.
 * @author Campbell Suter <znix@znix.xyz>
 */

#include <util/u_worker.hpp>
#include <math/m_space.h>
#include <math/m_vec3.h>

#include "catch/catch.hpp"

#include <thread>
#include <chrono>

TEST_CASE("CorrectPoseInverse")
{
	// Test that inverting a pose works correctly
	// Pick an arbitrary and non-trivial original pose
	struct xrt_pose orig = {};
	orig.position = {123.f, 456.f, 789.f};
	orig.orientation = {-0.439, -0.561, 0.072, -0.698};
	math_quat_normalize(&orig.orientation);

	// Invert it
	struct xrt_pose invert;
	math_pose_invert(&orig, &invert);

	// Multiply the poses together in both orders
	struct xrt_pose out_a, out_b;
	math_pose_transform(&orig, &invert, &out_a);
	math_pose_transform(&invert, &orig, &out_b);

	// A pose multiplied by it's inverse or vice-verse should have both a negligible rotation and position
	CHECK(m_vec3_len(out_a.position) < 0.001);
	CHECK(1 - abs(out_a.orientation.w) < 0.001);

	CHECK(m_vec3_len(out_b.position) < 0.001);
	CHECK(1 - abs(out_b.orientation.w) < 0.001);
}
