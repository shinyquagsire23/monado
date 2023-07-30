// Copyright 2022, Collabora, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Test for Levenberg-Marquardt kinematic optimizer
 * @author Moses Turner <moses@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */
#include "util/u_logging.h"
#include "xrt/xrt_defines.h"
#include <util/u_worker.hpp>
#include <math/m_mathinclude.h>
#include <math/m_space.h>
#include <math/m_vec3.h>
#include <math/m_vec2.h>

#include "kine_common.hpp"
#include "lm_interface.hpp"

#include "catch/catch.hpp"

#include <thread>
#include <chrono>
#include "fenv.h"

using namespace xrt::tracking::hand::mercury;

TEST_CASE("LevenbergMarquardt")
{
	// This does very little at the moment:
	// * It will explode if any floating point exceptions are generated
	// * You should run it with `valgrind --track-origins=yes` (and compile without optimizations so that origin
	// tracking works well) to see if we are using any uninitialized values.

	fetestexcept(FE_ALL_EXCEPT);

	struct one_frame_input input = {};

	for (int view = 0; view < 2; view++) {
		input.views[view].active = true;
		input.views[view].stereographic_radius = 0.5;
		input.views[view].look_dir = XRT_QUAT_IDENTITY;
		for (int i = 0; i < 5; i++) {

			input.views[view].curls[i].value = -0.5f;
			input.views[view].curls[i].variance = 1.0f;
		}
		for (int i = 0; i < 21; i++) {

			xrt_vec2 dir = {sinf(i), cosf(i)};
			m_vec2_normalize(&dir);

			input.views[view].keypoints_in_scaled_stereographic[i].pos_2d = dir; //{0,(float)i,-1};
			input.views[view].keypoints_in_scaled_stereographic[i].depth_relative_to_midpxm =
			    (i / 21.0f) - 0.5;
			input.views[view].keypoints_in_scaled_stereographic[i].confidence_depth = 1.0f;
			input.views[view].keypoints_in_scaled_stereographic[i].confidence_xy = 1.0f;
		}
	}

	lm::KinematicHandLM *hand;

	xrt_pose left_in_right = XRT_POSE_IDENTITY;
	left_in_right.position.x = 1;

	lm::optimizer_create(left_in_right, false, U_LOGGING_TRACE, &hand);


	xrt_hand_joint_set out = {};
	float out_hand_size = 0.0f;
	float out_reprojection_error = 0.0f;
	lm::optimizer_run(hand,          //
	                  input,         //
	                  true,          //
	                  2.0f,          //
	                  true,          //
	                  0.09,          //
	                  0.5,           //
	                  0.5f,          //
	                  out,           //
	                  out_hand_size, //
	                  out_reprojection_error);

	CHECK(std::isfinite(out_reprojection_error));
	CHECK(std::isfinite(out_hand_size));
}
