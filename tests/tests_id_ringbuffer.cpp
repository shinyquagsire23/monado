// Copyright 2021-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief u_id_ringbuffer collection tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#include <util/u_id_ringbuffer.h>



#include "catch/catch.hpp"


TEST_CASE("u_template_historybuf")
{
	auto *buffer = u_id_ringbuffer_create(4);
	SECTION("behavior when empty")
	{
		CHECK(u_id_ringbuffer_is_empty(buffer));
		CHECK(0 == u_id_ringbuffer_get_size(buffer));
		uint64_t out_id = 0;
		CHECK(u_id_ringbuffer_get_front(buffer, &out_id) < 0);
		CHECK(u_id_ringbuffer_get_back(buffer, &out_id) < 0);

		{
			INFO("Check after pop_back");
			u_id_ringbuffer_pop_back(buffer);
			CHECK(u_id_ringbuffer_is_empty(buffer));
		}
	}
	SECTION("behavior with one")
	{
		int64_t zero_inner_index;
		REQUIRE((zero_inner_index = u_id_ringbuffer_push_back(buffer, 0)) >= 0);
		CHECK_FALSE(u_id_ringbuffer_is_empty(buffer));
		CHECK(1 == u_id_ringbuffer_get_size(buffer));
		CAPTURE(zero_inner_index);


		// check front/back
		uint64_t out_id_front = 55;
		uint64_t out_id_back = 55;
		CHECK(u_id_ringbuffer_get_front(buffer, &out_id_front) >= 0);
		CHECK(u_id_ringbuffer_get_front(buffer, &out_id_front) == zero_inner_index);
		CHECK(out_id_front == 0);
		CHECK(u_id_ringbuffer_get_back(buffer, &out_id_back) >= 0);
		CHECK(u_id_ringbuffer_get_back(buffer, &out_id_front) == zero_inner_index);
		CHECK(out_id_back == 0);
		CHECK(out_id_front == out_id_back);

		// check contents
		{
			uint64_t out_id = 55;
			CHECK(u_id_ringbuffer_get_at_index(buffer, 0, &out_id) == zero_inner_index);
			CHECK(out_id == 0);
		}
		{
			uint64_t out_id = 55;
			CHECK(u_id_ringbuffer_get_at_age(buffer, 0, &out_id) == zero_inner_index);
			CHECK(out_id == 0);
		}
		{
			uint64_t out_id = 55;
			CHECK(u_id_ringbuffer_get_at_clamped_age(buffer, 0, &out_id) == zero_inner_index);
			CHECK(out_id == 0);
		}
		{
			uint64_t out_id = 55;
			CHECK(u_id_ringbuffer_get_at_age(buffer, 1, &out_id) < 0);
		}
		{
			uint64_t out_id = 55;
			CHECK(u_id_ringbuffer_get_at_clamped_age(buffer, 1, &out_id) == zero_inner_index);
			CHECK(out_id == 0);
		}
		{
			uint64_t out_id = 55;
			CHECK(u_id_ringbuffer_get_at_clamped_age(buffer, 2, &out_id) == zero_inner_index);
			CHECK(out_id == 0);
		}

		{
			INFO("Check after pop_back");
			u_id_ringbuffer_pop_back(buffer);

			CHECK(u_id_ringbuffer_is_empty(buffer));

			u_id_ringbuffer_pop_back(buffer);
			CHECK(u_id_ringbuffer_is_empty(buffer));
		}
	}

	SECTION("behavior with two")
	{
		int64_t zero_inner_index;
		int64_t one_inner_index;
		REQUIRE((zero_inner_index = u_id_ringbuffer_push_back(buffer, 0)) >= 0);
		REQUIRE((one_inner_index = u_id_ringbuffer_push_back(buffer, 1)) >= 0);
		REQUIRE(zero_inner_index != one_inner_index);
		CAPTURE(zero_inner_index);
		CAPTURE(one_inner_index);
		CHECK_FALSE(u_id_ringbuffer_is_empty(buffer));
		CHECK(2 == u_id_ringbuffer_get_size(buffer));

		SECTION("check front and back")
		{
			// check front/back
			uint64_t out_id_front = 55;
			uint64_t out_id_back = 55;
			CHECK(u_id_ringbuffer_get_front(buffer, &out_id_front) == zero_inner_index);
			CHECK(out_id_front == 0);
			CHECK(u_id_ringbuffer_get_back(buffer, &out_id_back) == one_inner_index);
			CHECK(out_id_back == 1);
			CHECK(out_id_front != out_id_back);
		}
		SECTION("check contents")
		{
			// check contents
			{
				uint64_t out_id = 55;
				CHECK(u_id_ringbuffer_get_at_index(buffer, 0, &out_id) == zero_inner_index);
				CHECK(out_id == 0);
			}
			{
				uint64_t out_id = 55;
				CHECK(u_id_ringbuffer_get_at_index(buffer, 1, &out_id) == one_inner_index);
				CHECK(out_id == 1);
			}
			{
				uint64_t out_id = 55;
				CHECK(u_id_ringbuffer_get_at_index(buffer, 2, &out_id) < 0);
			}

			{
				uint64_t out_id = 55;
				CHECK(u_id_ringbuffer_get_at_age(buffer, 0, &out_id) == one_inner_index);
				CHECK(out_id == 1);
			}
			{
				uint64_t out_id = 55;
				CHECK(u_id_ringbuffer_get_at_clamped_age(buffer, 0, &out_id) == one_inner_index);
				CHECK(out_id == 1);
			}

			{
				uint64_t out_id = 55;
				CHECK(u_id_ringbuffer_get_at_age(buffer, 1, &out_id) == zero_inner_index);
				CHECK(out_id == 0);
			}
			{
				uint64_t out_id = 55;
				CHECK(u_id_ringbuffer_get_at_clamped_age(buffer, 1, &out_id) == zero_inner_index);
				CHECK(out_id == 0);
			}

			{
				uint64_t out_id = 55;
				CHECK(u_id_ringbuffer_get_at_age(buffer, 2, &out_id) < 0);
			}
			{
				uint64_t out_id = 55;
				CHECK(u_id_ringbuffer_get_at_clamped_age(buffer, 2, &out_id) == zero_inner_index);
				CHECK(out_id == 0);
			}
			{
				uint64_t out_id = 55;
				CHECK(u_id_ringbuffer_get_at_clamped_age(buffer, 3, &out_id) == zero_inner_index);
				CHECK(out_id == 0);
			}
		}
		SECTION("Check after pop_back")
		{
			u_id_ringbuffer_pop_back(buffer);
			CHECK(1 == u_id_ringbuffer_get_size(buffer));
			uint64_t out_id_front = 55;
			CHECK(u_id_ringbuffer_get_front(buffer, &out_id_front) == zero_inner_index);
			CHECK(out_id_front == 0);

			u_id_ringbuffer_pop_back(buffer);
			CHECK(0 == u_id_ringbuffer_get_size(buffer));
			uint64_t out_id = 0;
			CHECK(u_id_ringbuffer_get_front(buffer, &out_id) < 0);
		}
	}

	SECTION("algorithm behavior with 3")
	{
		int64_t zero_inner_index;
		int64_t two_inner_index;
		int64_t four_inner_index;
		REQUIRE((zero_inner_index = u_id_ringbuffer_push_back(buffer, 0)) >= 0);
		REQUIRE((two_inner_index = u_id_ringbuffer_push_back(buffer, 2)) >= 0);
		REQUIRE((four_inner_index = u_id_ringbuffer_push_back(buffer, 4)) >= 0);
		CAPTURE(zero_inner_index);
		CAPTURE(two_inner_index);
		CAPTURE(four_inner_index);
		CHECK_FALSE(u_id_ringbuffer_is_empty(buffer));
		CHECK(3 == u_id_ringbuffer_get_size(buffer));

		uint64_t out_id = 55;
		uint32_t out_index = 55;
		CHECK(u_id_ringbuffer_find_id_unordered(buffer, 0, nullptr, nullptr) == zero_inner_index);
		CHECK(u_id_ringbuffer_find_id_unordered(buffer, 0, &out_id, &out_index) == zero_inner_index);
		CHECK(out_id == 0);
		CHECK(out_index == 0);

		CHECK(u_id_ringbuffer_find_id_unordered(buffer, 2, nullptr, nullptr) == two_inner_index);
		CHECK(u_id_ringbuffer_find_id_unordered(buffer, 2, &out_id, &out_index) == two_inner_index);
		CHECK(out_id == 2);
		CHECK(out_index == 1);

		CHECK(u_id_ringbuffer_find_id_unordered(buffer, 4, nullptr, nullptr) == four_inner_index);
		CHECK(u_id_ringbuffer_find_id_unordered(buffer, 4, &out_id, &out_index) == four_inner_index);
		CHECK(out_id == 4);
		CHECK(out_index == 2);

		// first id not less than 1 is id 2.
		out_id = 55;
		out_index = 55;
		CHECK(u_id_ringbuffer_lower_bound_id(buffer, 1, nullptr, nullptr) == two_inner_index);
		CHECK(u_id_ringbuffer_lower_bound_id(buffer, 1, &out_id, &out_index) == two_inner_index);
		CHECK(out_id == 2);
		CHECK(out_index == 1);
	}
	u_id_ringbuffer_destroy(&buffer);
}
