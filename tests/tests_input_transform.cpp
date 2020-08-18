// Copyright 2018-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Input transform tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#include "catch/catch.hpp"

#include <xrt/xrt_defines.h>

#include <oxr/oxr_input_transform.h>
#include <oxr/oxr_logger.h>
#include <oxr/oxr_objects.h>

using Catch::Generators::values;

TEST_CASE("input_transform")
{
	struct oxr_logger log;
	oxr_log_init(&log, "test");
	struct oxr_sink_logger slog = {};

	struct oxr_input_transform *transforms = NULL;
	size_t num_transforms = 0;

	oxr_input_value_tagged input = {};
	oxr_input_value_tagged output = {};

	SECTION("Float action")
	{
		XrActionType action_type = XR_ACTION_TYPE_FLOAT_INPUT;

		SECTION("From Vec1 -1 to 1 identity")
		{
			input.type = XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE;

			CHECK(oxr_input_transform_create_chain(&log, &slog, input.type, action_type, "float_action",
			                                       "/dummy_float", &transforms, &num_transforms));

			// Just identity
			CHECK(num_transforms == 1);
			CHECK(transforms != nullptr);

			SECTION("Roundtrip")
			{
				auto value = GENERATE(values({-1.f, -0.5f, 0.f, -0.f, 0.5f, 1.f}));
				input.value.vec1.x = value;

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(input.value.vec1.x == output.value.vec1.x);
			}
		}

		SECTION("From Vec1 0 to 1 identity")
		{
			input.type = XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE;

			CHECK(oxr_input_transform_create_chain(&log, &slog, input.type, action_type, "float_action",
			                                       "/dummy_float", &transforms, &num_transforms));

			// Just identity
			CHECK(num_transforms == 1);
			CHECK(transforms != nullptr);

			SECTION("Roundtrip")
			{
				auto value = GENERATE(values({0.f, -0.f, 0.5f, 1.f}));
				input.value.vec1.x = value;

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(input.value.vec1.x == output.value.vec1.x);
			}
		}

		SECTION("From Vec2 input")
		{
			input.type = XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE;
			input.value.vec2.x = -1;
			input.value.vec2.y = 1;

			SECTION("path component x")
			{
				CHECK(oxr_input_transform_create_chain(&log, &slog, input.type, action_type,
				                                       "float_action", "/dummy_vec2/x", &transforms,
				                                       &num_transforms));

				// A get-x
				CHECK(num_transforms == 1);
				CHECK(transforms != nullptr);

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(input.value.vec2.x == output.value.vec1.x);
			}

			SECTION("path component y")
			{
				CHECK(oxr_input_transform_create_chain(&log, &slog, input.type, action_type,
				                                       "float_action", "/dummy_vec2/y", &transforms,
				                                       &num_transforms));

				// A get-y
				CHECK(num_transforms == 1);
				CHECK(transforms != nullptr);

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(input.value.vec2.y == output.value.vec1.x);
			}

			SECTION("no component")
			{
				CHECK_FALSE(oxr_input_transform_create_chain(&log, &slog, input.type, action_type,
				                                             "float_action", "/dummy_vec2", &transforms,
				                                             &num_transforms));

				// Shouldn't make a transform, not possible
				CHECK(num_transforms == 0);
				CHECK(transforms == nullptr);

				// shouldn't do anything, but shouldn't explode.
				CHECK_FALSE(oxr_input_transform_process(transforms, num_transforms, &input, &output));
			}
		}

		SECTION("From bool input")
		{
			input.type = XRT_INPUT_TYPE_BOOLEAN;
			CHECK(oxr_input_transform_create_chain(&log, &slog, input.type, action_type, "float_action",
			                                       "/dummy_bool", &transforms, &num_transforms));

			// A bool-to-float
			CHECK(num_transforms == 1);
			CHECK(transforms != nullptr);

			SECTION("False")
			{
				input.value.boolean = false;

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(0.0f == output.value.vec1.x);
			}

			SECTION("True")
			{
				input.value.boolean = true;

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(1.0f == output.value.vec1.x);
			}
		}
	}

	SECTION("Bool action")
	{
		XrActionType action_type = XR_ACTION_TYPE_BOOLEAN_INPUT;
		SECTION("From Bool identity")
		{
			input.type = XRT_INPUT_TYPE_BOOLEAN;

			CHECK(oxr_input_transform_create_chain(&log, &slog, input.type, action_type, "bool_action",
			                                       "/dummy_bool", &transforms, &num_transforms));
			CHECK(num_transforms == 1);
			CHECK(transforms != nullptr);

			SECTION("Roundtrip")
			{
				auto value = GENERATE(values({0, 1}));
				input.value.boolean = bool(value);
				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(input.value.boolean == output.value.boolean);
			}
		}

		SECTION("From Vec1 -1 to 1")
		{
			input.type = XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE;

			CHECK(oxr_input_transform_create_chain(&log, &slog, input.type, action_type, "bool_action",
			                                       "/dummy_float", &transforms, &num_transforms));
			CHECK(num_transforms == 1);
			CHECK(transforms != nullptr);

			SECTION("True")
			{
				auto value = GENERATE(values({0.5f, 1.f}));
				input.value.vec1.x = value;

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(output.value.boolean == true);
			}

			SECTION("False")
			{
				auto value = GENERATE(values({0.0f, -1.f}));
				input.value.vec1.x = value;

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(output.value.boolean == false);
			}
		}

		SECTION("From Vec1 0 to 1")
		{
			input.type = XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE;

			CHECK(oxr_input_transform_create_chain(&log, &slog, input.type, action_type, "bool_action",
			                                       "/dummy_float", &transforms, &num_transforms));
			// A bool to float
			CHECK(num_transforms == 1);
			CHECK(transforms != nullptr);

			SECTION("True")
			{
				auto value = GENERATE(values({0.95f, 1.f}));
				input.value.vec1.x = value;

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(output.value.boolean == true);
			}

			SECTION("False")
			{
				auto value = GENERATE(values({0.0f, 0.5f}));
				input.value.vec1.x = value;

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(output.value.boolean == false);
			}
		}

		SECTION("From Vec2")
		{
			input.type = XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE;
			input.value.vec2.x = -1;
			input.value.vec2.y = 1;

			SECTION("x")
			{
				CHECK(oxr_input_transform_create_chain(&log, &slog, input.type, action_type,
				                                       "float_action", "/dummy_vec2/x", &transforms,
				                                       &num_transforms));
				CHECK(num_transforms == 2);
				CHECK(transforms != nullptr);

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(false == output.value.boolean);
			}

			SECTION("y")
			{
				CHECK(oxr_input_transform_create_chain(&log, &slog, input.type, action_type,
				                                       "float_action", "/dummy_vec2/y", &transforms,
				                                       &num_transforms));
				CHECK(num_transforms == 2);
				CHECK(transforms != nullptr);

				CHECK(oxr_input_transform_process(transforms, num_transforms, &input, &output));
				CHECK(true == output.value.boolean);
			}

			SECTION("no component")
			{
				CHECK_FALSE(oxr_input_transform_create_chain(&log, &slog, input.type, action_type,
				                                             "float_action", "/dummy", &transforms,
				                                             &num_transforms));

				// Shouldn't make a transform, not possible
				CHECK(num_transforms == 0);
				CHECK(transforms == nullptr);

				// shouldn't do anything, but shouldn't explode.
				CHECK_FALSE(oxr_input_transform_process(transforms, num_transforms, &input, &output));
			}
		}
	}

	SECTION("Pose action")
	{
		XrActionType action_type = XR_ACTION_TYPE_POSE_INPUT;

		SECTION("From Pose identity")
		{
			input.type = XRT_INPUT_TYPE_POSE;
			CHECK(oxr_input_transform_create_chain(&log, &slog, input.type, action_type, "pose_action",
			                                       "/dummy_pose", &transforms, &num_transforms));
			// Identity, just so this binding doesn't get culled.
			CHECK(num_transforms == 1);
		}

		SECTION("From other input")
		{
			auto input_type = GENERATE(values({
			    XRT_INPUT_TYPE_BOOLEAN,
			    XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE,
			    XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE,
			    XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE,
			    XRT_INPUT_TYPE_VEC3_MINUS_ONE_TO_ONE,
			}));

			CAPTURE(input_type);
			input.type = input_type;

			CHECK_FALSE(oxr_input_transform_create_chain(&log, &slog, input.type, action_type,
			                                             "pose_action", "/dummy", &transforms,
			                                             &num_transforms));

			// not possible
			CHECK(num_transforms == 0);
			CHECK(transforms == nullptr);
		}
	}

	oxr_log_slog(&log, &slog);
	oxr_input_transform_destroy(&transforms);
	CHECK(NULL == transforms);
}
