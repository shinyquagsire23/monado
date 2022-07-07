// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Test u_vector C interface.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 */

#include "catch/catch.hpp"
#include "util/u_vector.h"

TEST_CASE("u_vector")
{
	SECTION("Test interface generated from macros")
	{
		struct u_vector_float vf = u_vector_float_create();
		CHECK(vf.ptr != NULL);

		constexpr float A = 2.71f;
		constexpr float B = 1.61f;
		constexpr float C = 3.14f;

		u_vector_float_push_back(vf, A);
		u_vector_float_push_back(vf, B);
		u_vector_float_push_back(vf, C);

		float a = u_vector_float_at(vf, 0);
		float b = u_vector_float_at(vf, 1);
		float c = u_vector_float_at(vf, 2);

		CHECK(a == A);
		CHECK(b == B);
		CHECK(c == C);

		u_vector_float_destroy(&vf);
		CHECK(vf.ptr == NULL);
	}
}
