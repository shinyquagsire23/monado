// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Utilities for tests involving time points and durations
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#pragma once

#include <iostream>
#include <chrono>
#include <sstream>
#include <functional>
#include <iomanip>

using unanoseconds = std::chrono::duration<uint64_t, std::nano>;


template <typename T>
static inline std::string
stringifyNanos(std::chrono::duration<T, std::nano> value)
{
	using namespace std::chrono;
	std::ostringstream oss;
	auto sec = duration_cast<duration<T>>(value);
	if (duration<T, std::nano>(sec) == value) {
		oss << sec.count() << "s";
		return oss.str();
	}

	auto millis = duration_cast<duration<T, std::milli>>(value);
	if (duration<T, std::nano>(millis) == value) {
		oss << millis.count() << "ms";
		return oss.str();
	}

	auto micros = duration_cast<duration<T, std::micro>>(value);
	if (duration<T, std::nano>(micros) == value) {
		oss << micros.count() << "us";
		return oss.str();
	}

	oss << value.count() << "ns";
	return oss.str();
}

static inline std::string
stringifyTimePoint(std::chrono::steady_clock::time_point tp)
{
	auto dur = tp.time_since_epoch();
	auto hr = std::chrono::duration_cast<std::chrono::hours>(dur);
	dur -= hr;
	auto sec = std::chrono::duration_cast<std::chrono::seconds>(dur);
	dur -= sec;
	std::ostringstream oss;
	if (hr.count() > 0) {
		oss << hr.count() << ":";
	}
	oss << sec.count() << ".";
	using three_commas =
	    std::ratio_multiply<std::ratio<1, 1000>, std::ratio_multiply<std::ratio<1, 1000>, std::ratio<1, 1000>>>;
	static_assert(std::ratio_equal<three_commas, std::nano>::value);
	// 9 because of the preceding static assert: there's no compile-time rational log10? :-O
	oss << std::setfill('0') << std::setw(9);
	oss << dur.count();
	return oss.str();
}

namespace Catch {
template <> struct StringMaker<unanoseconds>
{
	static std::string
	convert(unanoseconds const &value)
	{
		return stringifyNanos(value);
	}
};
template <> struct StringMaker<std::chrono::nanoseconds>
{
	static std::string
	convert(std::chrono::nanoseconds const &value)
	{
		return stringifyNanos(value);
	}
};
template <> struct StringMaker<std::chrono::steady_clock::time_point>
{
	static std::string
	convert(std::chrono::steady_clock::time_point const &value)
	{
		return stringifyTimePoint(value);
	}
};
} // namespace Catch


class MockClock
{
public:
	uint64_t
	now() const noexcept
	{
		return std::chrono::duration_cast<unanoseconds>(now_.time_since_epoch()).count();
	}

	std::chrono::steady_clock::time_point
	now_typed() const noexcept
	{
		return now_;
	}

	void
	advance(unanoseconds ns)
	{
		now_ += ns;
	}

	void
	advance_to(uint64_t timestamp_ns)
	{
		CHECK(now() <= timestamp_ns);
		now_ = std::chrono::steady_clock::time_point(
		    std::chrono::steady_clock::duration(unanoseconds(timestamp_ns)));
	}

private:
	std::chrono::steady_clock::time_point now_{std::chrono::steady_clock::duration(std::chrono::seconds(1000000))};
};

struct FutureEvent
{
	std::chrono::steady_clock::time_point time_point;
	std::function<void()> action;
};
