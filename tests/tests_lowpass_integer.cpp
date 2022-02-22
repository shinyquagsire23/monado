// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Integer low pass filter tests.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#include <math/m_lowpass_integer.hpp>
#include <math/m_lowpass_integer.h>

#include "catch/catch.hpp"

using xrt::auxiliary::math::IntegerLowPassIIRFilter;
using xrt::auxiliary::math::Rational;
static constexpr uint16_t InitialState = 300;

TEMPLATE_TEST_CASE("IntegerLowPassIIRFilter", "", int32_t, uint32_t)
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

TEST_CASE("m_lowpass_integer")
{

	struct m_lowpass_integer *filter = m_lowpass_integer_create(1, 2);
	CHECK(filter);

	CHECK_FALSE(m_lowpass_integer_is_initialized(filter));

	m_lowpass_integer_add_sample(filter, InitialState);
	REQUIRE(m_lowpass_integer_is_initialized(filter));
	CHECK(m_lowpass_integer_get_state(filter) == InitialState);


	auto prev = m_lowpass_integer_get_state(filter);
	SECTION("Increase")
	{
		constexpr auto newTarget = InitialState * 2;
		for (int i = 0; i < 20; ++i) {
			m_lowpass_integer_add_sample(filter, newTarget);
			REQUIRE(m_lowpass_integer_is_initialized(filter));
			// not going to exceed this
			if (prev == newTarget || prev == newTarget - 1) {
				REQUIRE(m_lowpass_integer_get_state(filter) == prev);
			} else {
				REQUIRE(m_lowpass_integer_get_state(filter) > prev);
				prev = m_lowpass_integer_get_state(filter);
			}
		}
	}

	SECTION("Decrease")
	{
		constexpr auto newTarget = InitialState / 2;
		for (int i = 0; i < 20; ++i) {

			m_lowpass_integer_add_sample(filter, newTarget);
			REQUIRE(m_lowpass_integer_is_initialized(filter));
			if (prev == newTarget) {
				REQUIRE(m_lowpass_integer_get_state(filter) == newTarget);
			} else {
				REQUIRE(m_lowpass_integer_get_state(filter) < prev);
				prev = m_lowpass_integer_get_state(filter);
			}
		}
	}
	SECTION("Stay Same")
	{
		for (int i = 0; i < 20; ++i) {

			m_lowpass_integer_add_sample(filter, InitialState);
			REQUIRE(m_lowpass_integer_is_initialized(filter));
			REQUIRE(m_lowpass_integer_get_state(filter) == InitialState);
		}
	}
}
