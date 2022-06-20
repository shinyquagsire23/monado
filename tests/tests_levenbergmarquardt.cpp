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
#include <math/m_space.h>
#include <math/m_vec3.h>

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

	for (int i = 0; i < 21; i++) {

		input.views[0].rays[i] = m_vec3_normalize({0, (float)i, -1}); //{0,(float)i,-1};
		input.views[1].rays[i] = m_vec3_normalize({(float)i, 0, -1});
		input.views[0].confidences[i] = 1;
		input.views[1].confidences[i] = 1;
	}

	lm::KinematicHandLM *hand;

	xrt_pose left_in_right = XRT_POSE_IDENTITY;
	left_in_right.position.x = 1;

	lm::optimizer_create(left_in_right, false, U_LOGGING_TRACE, &hand);


	xrt_hand_joint_set out;
	float out_hand_size;
	float out_reprojection_error;
	lm::optimizer_run(hand, input, true, true, 0.09, 0.5, out, out_hand_size, out_reprojection_error);

	CHECK(std::isfinite(out_reprojection_error));
	CHECK(std::isfinite(out_hand_size));
}
