// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Test u_deque C interface.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 */

#include "catch/catch.hpp"
#include "util/u_deque.h"

TEST_CASE("u_deque")
{
	SECTION("Test interface generated from macros")
	{
		struct u_deque_timepoint_ns dt = u_deque_timepoint_ns_create();
		CHECK(dt.ptr != NULL);

		constexpr timepoint_ns A = 11111111;
		constexpr timepoint_ns B = 22222222;
		constexpr timepoint_ns C = 33333333;
		timepoint_ns elem = 0;

		CHECK(!u_deque_timepoint_ns_pop_front(dt, &elem));
		CHECK(elem == 0);

		u_deque_timepoint_ns_push_back(dt, C);
		u_deque_timepoint_ns_push_back(dt, A);

		CHECK(u_deque_timepoint_ns_pop_front(dt, &elem));
		CHECK(elem == C);
		CHECK(u_deque_timepoint_ns_size(dt) == 1);

		u_deque_timepoint_ns_push_back(dt, B);
		u_deque_timepoint_ns_push_back(dt, C);

		timepoint_ns a = u_deque_timepoint_ns_at(dt, 0);
		timepoint_ns b = u_deque_timepoint_ns_at(dt, 1);
		timepoint_ns c = u_deque_timepoint_ns_at(dt, 2);

		CHECK(a == A);
		CHECK(b == B);
		CHECK(c == C);

		CHECK(u_deque_timepoint_ns_size(dt) == 3);

		CHECK(u_deque_timepoint_ns_pop_front(dt, &elem));
		CHECK(elem == A);
		CHECK(u_deque_timepoint_ns_pop_front(dt, &elem));
		CHECK(elem == B);
		CHECK(u_deque_timepoint_ns_pop_front(dt, &elem));
		CHECK(elem == C);

		CHECK(u_deque_timepoint_ns_size(dt) == 0);

		u_deque_timepoint_ns_destroy(&dt);
		CHECK(dt.ptr == NULL);
	}
}
