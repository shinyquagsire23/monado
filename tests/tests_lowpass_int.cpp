// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Integer low pass filter tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#include <tracking/t_lowpass_integer.hpp>

#include "catch/catch.hpp"


using xrt::auxiliary::math::Rational;
using xrt::auxiliary::tracking::IntegerLowPassIIRFilter;
static constexpr uint16_t InitialState = 300;

TEMPLATE_TEST_CASE("t_lowpass_integer", "", int32_t, uint32_t)
{
	IntegerLowPassIIRFilter<TestType> filter(Rational<TestType>{1, 2});

	CHECK_FALSE(filter.isInitialized());

	filter.addSample(InitialState);
	CHECK(filter.getState() == InitialState);
	CHECK(filter.isInitialized());

	auto prev = filter.getState();
	SECTION("Increase")
	{
		constexpr auto newTarget = InitialState * 2;
		for (int i = 0; i < 20; ++i) {
			filter.addSample(newTarget);
			REQUIRE(filter.isInitialized());
			// not going to exceed this
			if (prev == newTarget || prev == newTarget - 1) {
				REQUIRE(filter.getState() == prev);
			} else {
				REQUIRE(filter.getState() > prev);
				prev = filter.getState();
			}
		}
	}

	SECTION("Decrease")
	{
		constexpr auto newTarget = InitialState / 2;
		for (int i = 0; i < 20; ++i) {

			filter.addSample(newTarget);
			REQUIRE(filter.isInitialized());
			if (prev == newTarget) {
				REQUIRE(filter.getState() == newTarget);
			} else {
				REQUIRE(filter.getState() < prev);
				prev = filter.getState();
			}
		}
	}
	SECTION("Stay Same")
	{
		for (int i = 0; i < 20; ++i) {

			filter.addSample(InitialState);
			REQUIRE(filter.isInitialized());
			REQUIRE(filter.getState() == InitialState);
		}
	}
}
