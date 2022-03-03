// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Thread pool tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#include <util/u_worker.hpp>

#include "catch/catch.hpp"

#include <thread>
#include <chrono>

using namespace std::chrono_literals;


using namespace xrt::auxiliary::util;

TEST_CASE("TaskCollection")
{
	SharedThreadPool pool{2, 3};
	bool calledA[] = {
	    false,
	    false,
	    false,
	};

	bool calledB[] = {
	    false,
	    false,
	    false,
	};

	std::vector<TaskCollection::Functor> funcsA = {
	    [&] { calledA[0] = true; },
	    [&] { calledA[1] = true; },
	    [&] { calledA[2] = true; },
	};

	std::vector<TaskCollection::Functor> funcsB = {
	    [&] { calledB[0] = true; },
	    [&] { calledB[1] = true; },
	    [&] { calledB[2] = true; },
	};

	SharedThreadGroup groupA{pool};
	SharedThreadGroup groupB{pool};

	CHECK(!calledA[0]);
	CHECK(!calledA[1]);
	CHECK(!calledA[2]);

	TaskCollection collectionA{groupA, funcsA};

	SECTION("Sequential wait")
	{
		collectionA.waitAll();
		CHECK(calledA[0]);
		CHECK(calledA[1]);
		CHECK(calledA[2]);

		CHECK(!calledB[0]);
		CHECK(!calledB[1]);
		CHECK(!calledB[2]);

		{
			TaskCollection collectionB{groupB, funcsB};
		}
		CHECK(calledB[0]);
		CHECK(calledB[1]);
		CHECK(calledB[2]);
	}

	SECTION("Simultaneous dispatch, reversed wait")
	{
		CHECK(!calledB[0]);
		CHECK(!calledB[1]);
		CHECK(!calledB[2]);

		{
			TaskCollection collectionB{groupB, funcsB};
		}

		CHECK(calledB[0]);
		CHECK(calledB[1]);
		CHECK(calledB[2]);

		collectionA.waitAll();
		CHECK(calledA[0]);
		CHECK(calledA[1]);
		CHECK(calledA[2]);
	}


	SECTION("Simultaneous dispatch, reversed wait")
	{
		CHECK(!calledB[0]);
		CHECK(!calledB[1]);
		CHECK(!calledB[2]);

		{
			TaskCollection collectionB{
			    groupB,
			    {
			        [&] {
				        std::this_thread::sleep_for(500ms);
				        calledB[0] = true;
			        },
			        [&] {
				        std::this_thread::sleep_for(500ms);
				        calledB[1] = true;
			        },
			        [&] {
				        std::this_thread::sleep_for(500ms);
				        calledB[2] = true;
			        },
			    },
			};
		}

		CHECK(calledB[0]);
		CHECK(calledB[1]);
		CHECK(calledB[2]);

		collectionA.waitAll();
		CHECK(calledA[0]);
		CHECK(calledA[1]);
		CHECK(calledA[2]);
	}
}
