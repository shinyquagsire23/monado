// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Scalar float low pass filter tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#include <math/m_lowpass_float.hpp>

#include "catch/catch.hpp"

using xrt::auxiliary::math::LowPassIIRFilter;
static constexpr float InitialState = 300;
static constexpr timepoint_ns InitialTime = 12345;
static constexpr timepoint_ns StepSize = U_TIME_1MS_IN_NS * 20;

TEMPLATE_TEST_CASE("LowPassIIRFilter", "", float, double)
{
	LowPassIIRFilter<TestType> filter(100);

	CHECK_FALSE(filter.isInitialized());
	timepoint_ns now = InitialTime;

	filter.addSample(InitialState, now);
	CHECK(filter.getState() == InitialState);
	CHECK(filter.getTimestampNs() == now);
	CHECK(filter.isInitialized());

	auto prev = filter.getState();
	SECTION("Increase")
	{
		constexpr auto newTarget = InitialState * 2;
		for (int i = 0; i < 20; ++i) {
			now += StepSize;
			filter.addSample(newTarget, now);
			REQUIRE(filter.isInitialized());
			CHECK(filter.getTimestampNs() == now);
			// not going to exceed this
			if (prev == newTarget) {
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
			now += StepSize;
			filter.addSample(newTarget, now);
			REQUIRE(filter.isInitialized());
			CHECK(filter.getTimestampNs() == now);
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
			now += StepSize;
			filter.addSample(InitialState, now);
			REQUIRE(filter.isInitialized());
			CHECK(filter.getTimestampNs() == now);
			REQUIRE(filter.getState() == InitialState);
		}
	}
}
