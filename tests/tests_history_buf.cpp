// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief HistoryBuffer collection tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#include <math/m_relation_history.h>
#include <util/u_time.h>
#include <util/u_template_historybuf.hpp>
#include <iostream>


using xrt::auxiliary::util::HistoryBuffer;

template <typename Container>
static inline std::ostream &
operator<<(std::ostream &os, const xrt::auxiliary::util::RandomAccessIteratorBase<Container> &iter_base)
{
	os << "Iterator@[" << iter_base.index() << "]";
	return os;
}


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
		relation.relation_flags = (xrt_space_relation_flags)( //
		    XRT_SPACE_RELATION_POSITION_TRACKED_BIT |         //
		    XRT_SPACE_RELATION_POSITION_VALID_BIT |           //
		    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |      //
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |        //
		    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT);    //
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


TEST_CASE("RelationHistory")
{
	using xrt::auxiliary::math::RelationHistory;
	RelationHistory rh;

	SECTION("empty buffer")
	{
		xrt_space_relation out_relation = XRT_SPACE_RELATION_ZERO;
		CHECK(rh.get(0, &out_relation) == M_RELATION_HISTORY_RESULT_INVALID);
		CHECK(rh.get(1, &out_relation) == M_RELATION_HISTORY_RESULT_INVALID);
	}
	SECTION("populated buffer")
	{
		xrt_space_relation relation = XRT_SPACE_RELATION_ZERO;
		relation.relation_flags = (xrt_space_relation_flags)( //
		    XRT_SPACE_RELATION_POSITION_TRACKED_BIT |         //
		    XRT_SPACE_RELATION_POSITION_VALID_BIT |           //
		    XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |      //
		    XRT_SPACE_RELATION_ORIENTATION_VALID_BIT |        //
		    XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT);    //
		relation.linear_velocity.x = 1.f;

		// arbitrary value
		constexpr auto T0 = 20 * (uint64_t)U_TIME_1S_IN_NS;
		// one second after T0
		constexpr auto T1 = T0 + (uint64_t)U_TIME_1S_IN_NS;
		// two seconds after T0
		constexpr auto T2 = T1 + (uint64_t)U_TIME_1S_IN_NS;

		xrt_space_relation out_relation = XRT_SPACE_RELATION_ZERO;
		uint64_t out_time = 0;

		CHECK(rh.size() == 0);
		CHECK_FALSE(rh.get_latest(&out_time, &out_relation));


		CHECK(rh.push(relation, T0));
		CHECK(rh.size() == 1);
		CHECK(rh.get_latest(&out_time, &out_relation));
		CHECK(out_time == T0);

		relation.pose.position.x = 1.f;
		CHECK(rh.push(relation, T1));
		CHECK(rh.size() == 2);
		CHECK(rh.get_latest(&out_time, &out_relation));
		CHECK(out_time == T1);

		relation.pose.position.x = 2.f;
		CHECK(rh.push(relation, T2));
		CHECK(rh.size() == 3);
		CHECK(rh.get_latest(&out_time, &out_relation));
		CHECK(out_time == T2);

		// Try going back in time: should fail to push, leave state the same
		CHECK_FALSE(rh.push(relation, T1));
		CHECK(rh.size() == 3);
		CHECK(rh.get_latest(&out_time, &out_relation));
		CHECK(out_time == T2);

		CHECK(rh.get(0, &out_relation) == M_RELATION_HISTORY_RESULT_INVALID);

		CHECK(rh.get(T0, &out_relation) == M_RELATION_HISTORY_RESULT_EXACT);
		CHECK(out_relation.pose.position.x == 0.f);

		CHECK(rh.get(T1, &out_relation) == M_RELATION_HISTORY_RESULT_EXACT);
		CHECK(out_relation.pose.position.x == 1.f);

		CHECK(rh.get(T2, &out_relation) == M_RELATION_HISTORY_RESULT_EXACT);
		CHECK(out_relation.pose.position.x == 2.f);


		CHECK(rh.get(T0 - (uint64_t)U_TIME_1S_IN_NS, &out_relation) ==
		      M_RELATION_HISTORY_RESULT_REVERSE_PREDICTED);
		CHECK(out_relation.pose.position.x < 0.f);

		CHECK(rh.get((T0 + T1) / 2, &out_relation) == M_RELATION_HISTORY_RESULT_INTERPOLATED);
		CHECK(out_relation.pose.position.x > 0.f);
		CHECK(out_relation.pose.position.x < 1.f);

		CHECK(rh.get((T1 + T2) / 2, &out_relation) == M_RELATION_HISTORY_RESULT_INTERPOLATED);
		CHECK(out_relation.pose.position.x > 1.f);
		CHECK(out_relation.pose.position.x < 2.f);

		CHECK(rh.get(T2 + (uint64_t)U_TIME_1S_IN_NS, &out_relation) == M_RELATION_HISTORY_RESULT_PREDICTED);
		CHECK(out_relation.pose.position.x > 2.f);
	}
}

TEST_CASE("u_template_historybuf")
{
	HistoryBuffer<int, 4> buffer;
	SECTION("behavior when empty")
	{
		CHECK(buffer.empty());
		CHECK(0 == buffer.size()); // NOLINT
		CHECK_FALSE(buffer.begin().valid());
		CHECK_FALSE(buffer.end().valid());
		CHECK(buffer.begin() == buffer.end());
		{
			INFO("Check after pop_back");
			REQUIRE_FALSE(buffer.pop_back());
			CHECK(buffer.empty());
		}
	}
	SECTION("behavior with one")
	{
		buffer.push_back(0);
		CHECK_FALSE(buffer.empty());
		CHECK(buffer.size() == 1);

		// check iterators
		CHECK(buffer.begin().valid());
		CHECK_FALSE(buffer.end().valid());
		CHECK_FALSE(buffer.begin() == buffer.end());
		{
			auto it = buffer.end();
			// should be permanently cleared
			++it;
			CHECK_FALSE(it.valid());
			--it;
			CHECK_FALSE(it.valid());
		}
		CHECK(buffer.begin() == buffer.cbegin());
		CHECK(buffer.end() == buffer.cend());

		// can we decrement our past-the-end iterator to get the begin iterator?
		CHECK(buffer.begin() == --(buffer.end()));

		// make sure post-decrement works right
		CHECK_FALSE(buffer.begin() == (buffer.end())--);

		// make sure post-increment works right
		CHECK(buffer.begin() == buffer.begin()++);

		// make sure pre-increment works right
		CHECK_FALSE(buffer.begin() == ++(buffer.begin()));


		// check contents
		CHECK_NOTHROW(buffer.get_at_index(0));
		CHECK_FALSE(buffer.get_at_index(0) == nullptr);
		CHECK(*buffer.get_at_index(0) == 0);

		CHECK_FALSE(buffer.get_at_age(0) == nullptr);
		CHECK(*buffer.get_at_age(0) == 0);
		CHECK_FALSE(buffer.get_at_clamped_age(0) == nullptr);
		CHECK(*buffer.get_at_clamped_age(0) == 0);

		CHECK(buffer.get_at_age(1) == nullptr);

		CHECK_FALSE(buffer.get_at_clamped_age(1) == nullptr);
		CHECK(*buffer.get_at_clamped_age(1) == 0);

		CHECK_FALSE(buffer.get_at_clamped_age(2) == nullptr);
		CHECK(*buffer.get_at_clamped_age(2) == 0);

		CHECK_NOTHROW(buffer.front());
		CHECK(buffer.front() == 0);

		CHECK_NOTHROW(buffer.back());
		CHECK(buffer.back() == 0);

		CHECK(*buffer.begin() == buffer.front());

		{
			INFO("Check after pop_back");
			REQUIRE(buffer.pop_back());
			CHECK(buffer.size() == 0);

			REQUIRE_FALSE(buffer.pop_back());
		}
	}

	SECTION("behavior with two")
	{
		buffer.push_back(0);
		buffer.push_back(1);
		CHECK_FALSE(buffer.empty());
		CHECK(buffer.size() == 2);
		SECTION("check iterators")
		{
			// check iterators
			CHECK(buffer.begin().valid());
			CHECK_FALSE(buffer.end().valid());
			CHECK_FALSE(buffer.begin() == buffer.end());
			{
				auto it = buffer.end();
				// should be permanently cleared
				++it;
				CHECK_FALSE(it.valid());
				--it;
				CHECK_FALSE(it.valid());
			}
			CHECK(buffer.begin() == buffer.cbegin());
			CHECK(buffer.end() == buffer.cend());

			// can we decrement our past-the-end iterator to get the begin iterator?
			CHECK(buffer.begin() == --(--(buffer.end())));

			// make sure post-decrement works right
			CHECK_FALSE(buffer.begin() == (buffer.end())--);

			// make sure post-increment works right
			CHECK(buffer.begin() == buffer.begin()++);

			// make sure pre-increment works right
			CHECK_FALSE(buffer.begin() == ++(buffer.begin()));
		}
		SECTION("check contents")
		{
			// check contents
			CHECK_NOTHROW(buffer.get_at_index(0));
			CHECK_FALSE(buffer.get_at_index(0) == nullptr);
			CHECK(*buffer.get_at_index(0) == 0);
			CHECK_FALSE(buffer.get_at_index(1) == nullptr);
			CHECK(*buffer.get_at_index(1) == 1);
			CHECK(buffer.get_at_index(2) == nullptr);

			CHECK_NOTHROW(buffer.get_at_age(0));
			CHECK_FALSE(buffer.get_at_age(0) == nullptr);
			CHECK(*buffer.get_at_age(0) == 1);
			CHECK_FALSE(buffer.get_at_clamped_age(0) == nullptr);
			CHECK(*buffer.get_at_clamped_age(0) == 1);

			CHECK_FALSE(buffer.get_at_age(1) == nullptr);
			CHECK(*buffer.get_at_age(1) == 0);
			CHECK_FALSE(buffer.get_at_clamped_age(1) == nullptr);
			CHECK(*buffer.get_at_clamped_age(1) == 0);

			CHECK(buffer.get_at_age(2) == nullptr);

			CHECK_FALSE(buffer.get_at_clamped_age(2) == nullptr);
			CHECK(*buffer.get_at_clamped_age(2) == 0);

			CHECK_FALSE(buffer.get_at_clamped_age(3) == nullptr);
			CHECK(*buffer.get_at_clamped_age(3) == 0);

			CHECK_NOTHROW(buffer.front());
			CHECK(buffer.front() == 0);

			CHECK_NOTHROW(buffer.back());
			CHECK(buffer.back() == 1);

			CHECK(*buffer.begin() == buffer.front());
			CHECK(buffer.back() == *(--buffer.end()));
		}
		SECTION("Check after pop_back")
		{
			REQUIRE(buffer.pop_back());
			CHECK(buffer.size() == 1);
			CHECK(buffer.front() == 0);

			REQUIRE(buffer.pop_back());
			CHECK(buffer.size() == 0);
		}
	}

	SECTION("algorithm behavior with 3")
	{
		buffer.push_back(0);
		buffer.push_back(2);
		buffer.push_back(4);
		CHECK_FALSE(buffer.empty());
		CHECK(buffer.size() == 3);
		CHECK(buffer.begin() == std::find(buffer.begin(), buffer.end(), 0));
		CHECK(++(buffer.begin()) == std::find(buffer.begin(), buffer.end(), 2));
		CHECK(buffer.end() == std::find(buffer.begin(), buffer.end(), 5));

		CHECK(++(buffer.begin()) == std::lower_bound(buffer.begin(), buffer.end(), 1));
	}
}

TEST_CASE("IteratorBase")
{

	HistoryBuffer<int, 4> buffer;
	buffer.push_back(0);
	buffer.push_back(2);
	buffer.push_back(4);
	using namespace xrt::auxiliary::util;
	using const_iterator = typename HistoryBuffer<int, 4>::const_iterator;
	const_iterator default_constructed{};
	const_iterator begin_constructed = buffer.begin();
	const_iterator end_constructed = buffer.end();

	SECTION("Check default constructed")
	{
		CHECK_FALSE(default_constructed.valid());
		CHECK(default_constructed.is_cleared());
	}
	SECTION("Check begin constructed")
	{
		CHECK(begin_constructed.valid());
		CHECK_FALSE(begin_constructed.is_cleared());
		CHECK((--begin_constructed).is_cleared());
	}
	SECTION("Check end constructed")
	{
		CHECK_FALSE(end_constructed.valid());
		CHECK_FALSE(end_constructed.is_cleared());
		// if we go past the end, we can go backwards into validity.
		CHECK_FALSE((++end_constructed).is_cleared());
	}
}
