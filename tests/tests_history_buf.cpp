// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief HistoryBuffer collection tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#include <iostream>
#include <math/m_relation_history.h>

#include "catch/catch.hpp"



TEST_CASE("m_relation_history")
{
	m_relation_history *rh = nullptr;

	m_relation_history_create(&rh);
	SECTION("empty buffer")
	{
		xrt_space_relation out_relation = XRT_SPACE_RELATION_ZERO;
		CHECK(m_relation_history_get(rh, 0, &out_relation) == M_RELATION_HISTORY_RESULT_INVALID);
		CHECK(m_relation_history_get(rh, 1, &out_relation) == M_RELATION_HISTORY_RESULT_INVALID);
	}
	SECTION("populated buffer")
	{
		xrt_space_relation relation = XRT_SPACE_RELATION_ZERO;
		relation.relation_flags = (xrt_space_relation_flags)(
		    XRT_SPACE_RELATION_POSITION_TRACKED_BIT | XRT_SPACE_RELATION_POSITION_VALID_BIT |
		    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT | XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |
		    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT);
		relation.linear_velocity.x = 1.f;

		// arbitrary value
		constexpr auto T0 = 20 * (uint64_t)U_TIME_1S_IN_NS;
		// one second after T0
		constexpr auto T1 = T0 + (uint64_t)U_TIME_1S_IN_NS;
		// two seconds after T0
		constexpr auto T2 = T1 + (uint64_t)U_TIME_1S_IN_NS;

		xrt_space_relation out_relation = XRT_SPACE_RELATION_ZERO;
		uint64_t out_time = 0;

		CHECK(m_relation_history_get_size(rh) == 0);
		CHECK_FALSE(m_relation_history_get_latest(rh, &out_time, &out_relation));


		CHECK(m_relation_history_push(rh, &relation, T0));
		CHECK(m_relation_history_get_size(rh) == 1);
		CHECK(m_relation_history_get_latest(rh, &out_time, &out_relation));
		CHECK(out_time == T0);

		relation.pose.position.x = 1.f;
		CHECK(m_relation_history_push(rh, &relation, T1));
		CHECK(m_relation_history_get_size(rh) == 2);
		CHECK(m_relation_history_get_latest(rh, &out_time, &out_relation));
		CHECK(out_time == T1);

		relation.pose.position.x = 2.f;
		CHECK(m_relation_history_push(rh, &relation, T2));
		CHECK(m_relation_history_get_size(rh) == 3);
		CHECK(m_relation_history_get_latest(rh, &out_time, &out_relation));
		CHECK(out_time == T2);

		// Try going back in time: should fail to push, leave state the same
		CHECK_FALSE(m_relation_history_push(rh, &relation, T1));
		CHECK(m_relation_history_get_size(rh) == 3);
		CHECK(m_relation_history_get_latest(rh, &out_time, &out_relation));
		CHECK(out_time == T2);

		CHECK(m_relation_history_get(rh, 0, &out_relation) == M_RELATION_HISTORY_RESULT_INVALID);

		CHECK(m_relation_history_get(rh, T0, &out_relation) == M_RELATION_HISTORY_RESULT_EXACT);
		CHECK(out_relation.pose.position.x == 0.f);

		CHECK(m_relation_history_get(rh, T1, &out_relation) == M_RELATION_HISTORY_RESULT_EXACT);
		CHECK(out_relation.pose.position.x == 1.f);

		CHECK(m_relation_history_get(rh, T2, &out_relation) == M_RELATION_HISTORY_RESULT_EXACT);
		CHECK(out_relation.pose.position.x == 2.f);


		CHECK(m_relation_history_get(rh, T0 - (uint64_t)U_TIME_1S_IN_NS, &out_relation) ==
		      M_RELATION_HISTORY_RESULT_REVERSE_PREDICTED);
		CHECK(out_relation.pose.position.x < 0.f);

		CHECK(m_relation_history_get(rh, (T0 + T1) / 2, &out_relation) ==
		      M_RELATION_HISTORY_RESULT_INTERPOLATED);
		CHECK(out_relation.pose.position.x > 0.f);
		CHECK(out_relation.pose.position.x < 1.f);

		CHECK(m_relation_history_get(rh, (T1 + T2) / 2, &out_relation) ==
		      M_RELATION_HISTORY_RESULT_INTERPOLATED);
		CHECK(out_relation.pose.position.x > 1.f);
		CHECK(out_relation.pose.position.x < 2.f);

		CHECK(m_relation_history_get(rh, T2 + (uint64_t)U_TIME_1S_IN_NS, &out_relation) ==
		      M_RELATION_HISTORY_RESULT_PREDICTED);
		CHECK(out_relation.pose.position.x > 2.f);
	}


	m_relation_history_destroy(&rh);
}
