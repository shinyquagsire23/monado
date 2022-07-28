// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Time-keeping: a clock that is steady, convertible to system time, and
 * ideally high-resolution.
 *
 * Designed to suit the needs of OpenXR: you can and should use something
 * simpler (like @ref aux_os_time) for most purposes that aren't in OpenXR
 * interface code.
 *
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 *
 * @see time_state
 */

#pragma once

#include "xrt/xrt_compiler.h"

#include <stdint.h>
#include <time.h>


#ifdef __cplusplus
extern "C" {
#endif

//! Helper define to make code more readable.
#define U_1_000_000_000 (1000 * 1000 * 1000)

/*!
 * The number of nanoseconds in a second.
 *
 * @see timepoint_ns
 * @ingroup time_ns_to_s
 */
#define U_TIME_1S_IN_NS U_1_000_000_000

/*!
 * The number of nanoseconds in a millisecond.
 *
 * @see timepoint_ns
 */
#define U_TIME_1MS_IN_NS (1000 * 1000)

/*!
 * The number of nanoseconds in half a millisecond.
 *
 * @see timepoint_ns
 */
#define U_TIME_HALF_MS_IN_NS (U_TIME_1MS_IN_NS / 2)

/*!
 * Integer timestamp type.
 *
 * @see time_state
 * @see time_duration_ns
 * @ingroup aux_util
 */
typedef int64_t timepoint_ns;

/*!
 * Integer duration type in nanoseconds.
 *
 * Logical type of timepoint differences.
 *
 * @see time_state
 * @see timepoint_ns
 * @ingroup aux_util
 */
typedef int64_t time_duration_ns;

/*!
 * Convert nanoseconds duration to double seconds.
 *
 * @see timepoint_ns
 * @ingroup aux_util
 */
static inline double
time_ns_to_s(time_duration_ns ns)
{
	return (double)(ns) / (double)(U_TIME_1S_IN_NS);
}

/*!
 * Convert float seconds to nanoseconds.
 *
 * @see timepoint_ns
 * @ingroup aux_util
 */
static inline time_duration_ns
time_s_to_ns(double duration)
{
	return (time_duration_ns)(duration * (double)U_TIME_1S_IN_NS);
}

/*!
 * Convert nanoseconds to double float milliseconds, useful for printing.
 *
 * @see timepoint_ns
 * @ingroup aux_util
 */
static inline double
time_ns_to_ms_f(time_duration_ns ns)
{
	return (double)(ns) / (double)(U_TIME_1MS_IN_NS);
}

/*!
 * Checks if two timepoints are with a certain range of each other.
 *
 * @see timepoint_ns
 * @ingroup aux_util
 */
static inline bool
time_is_within_range_of_each_other(timepoint_ns a, timepoint_ns b, uint64_t range)
{
	int64_t t = (int64_t)a - (int64_t)b;
	return (-(int64_t)range < t) && (t < (int64_t)range);
}

/*!
 * Checks if two timepoints are with half a millisecond of each other.
 *
 * @see timepoint_ns
 * @ingroup aux_util
 */
static inline bool
time_is_within_half_ms(timepoint_ns a, timepoint_ns b)
{
	return time_is_within_range_of_each_other(a, b, U_TIME_HALF_MS_IN_NS);
}

/*!
 * Fuzzy comparisons.
 *
 * @see timepoint_ns
 * @ingroup aux_util
 */
static inline bool
time_is_less_then_or_within_range(timepoint_ns a, timepoint_ns b, uint64_t range)
{
	return a < b || time_is_within_range_of_each_other(a, b, range);
}

/*!
 * Fuzzy comparisons.
 *
 * @see timepoint_ns
 * @ingroup aux_util
 */
static inline bool
time_is_less_then_or_within_half_ms(timepoint_ns a, timepoint_ns b)
{
	return time_is_less_then_or_within_range(a, b, U_TIME_HALF_MS_IN_NS);
}

/*!
 * Fuzzy comparisons.
 *
 * @see timepoint_ns
 * @ingroup aux_util
 */
static inline bool
time_is_greater_then_or_within_range(timepoint_ns a, timepoint_ns b, uint64_t range)
{
	return a > b || time_is_within_range_of_each_other(a, b, range);
}

/*!
 * Fuzzy comparisons.
 *
 * @see timepoint_ns
 * @ingroup aux_util
 */
static inline bool
time_is_greater_then_or_within_half_ms(timepoint_ns a, timepoint_ns b)
{
	return time_is_greater_then_or_within_range(a, b, U_TIME_HALF_MS_IN_NS);
}

/*!
 * @struct time_state util/u_time.h
 * @brief Time-keeping state structure.
 *
 * Exposed as an opaque pointer.
 *
 * @see timepoint_ns
 * @ingroup aux_util
 */
struct time_state;

/*!
 * Create a struct time_state.
 *
 * @public @memberof time_state
 * @ingroup aux_util
 */
struct time_state *
time_state_create(uint64_t offset);


/*!
 * Destroy a struct time_state.
 *
 * Should not be called simultaneously with any other time_state function.
 *
 * @public @memberof time_state
 * @ingroup aux_util
 */
void
time_state_destroy(struct time_state **state);

/*!
 * Get the current time as an integer timestamp.
 *
 * Does not update internal state for timekeeping.
 * Should not be called simultaneously with time_state_get_now_and_update.
 *
 * @public @memberof time_state
 * @ingroup aux_util
 */
timepoint_ns
time_state_get_now(struct time_state const *state);

/*!
 * Get the current time as an integer timestamp and update internal state.
 *
 * This should be called regularly, but only from one thread.
 * It updates the association between the timing sources.
 *
 * Should not be called simultaneously with any other time_state function.
 *
 * @public @memberof time_state
 * @ingroup aux_util
 */
timepoint_ns
time_state_get_now_and_update(struct time_state *state);

/*!
 * Convert an integer timestamp to a struct timespec (system time).
 *
 * Should not be called simultaneously with time_state_get_now_and_update.
 *
 * @public @memberof time_state
 * @ingroup aux_util
 */
void
time_state_to_timespec(struct time_state const *state, timepoint_ns timestamp, struct timespec *out);

/*!
 * Convert a struct timespec (system time) to an integer timestamp.
 *
 * Should not be called simultaneously with time_state_get_now_and_update.
 *
 * @public @memberof time_state
 * @ingroup aux_util
 */
timepoint_ns
time_state_from_timespec(struct time_state const *state, const struct timespec *timespecTime);

/*!
 * Convert a monotonic system time (such as from @ref aux_os_time) to an
 * adjusted integer timestamp.
 *
 * Adjustments may need to be applied to achieve the other guarantees that e.g.
 * CLOCK_MONOTONIC does not provide: this function performs those adjustments.
 *
 * Should not be called simultaneously with time_state_get_now_and_update.
 *
 * @public @memberof time_state
 * @ingroup aux_util
 */
timepoint_ns
time_state_monotonic_to_ts_ns(struct time_state const *state, uint64_t monotonic_ns);

/*!
 * Convert a adjusted integer timestamp to an monotonic system time (such as
 * from @ref aux_os_time).
 *
 * Should not be called simultaneously with time_state_get_now_and_update.
 *
 * @public @memberof time_state
 * @ingroup aux_util
 */
uint64_t
time_state_ts_to_monotonic_ns(struct time_state const *state, timepoint_ns timestamp);

#if defined(XRT_OS_WINDOWS) || defined(XRT_DOXYGEN)
/*!
 * Converts a timestamp to Win32 "QPC" ticks.
 *
 * Should not be called simultaneously with time_state_get_now_and_update.
 *
 * @public @memberof time_state
 * @ingroup aux_util
 */
void
time_state_to_win32perfcounter(struct time_state const *state, timepoint_ns timestamp, LARGE_INTEGER *out_qpc_ticks);

/*!
 * Converts from Win32 "QPC" ticks to timestamp.
 *
 * Should not be called simultaneously with time_state_get_now_and_update.
 *
 * @public @memberof time_state
 * @ingroup aux_util
 */
timepoint_ns
time_state_from_win32perfcounter(struct time_state const *state, const LARGE_INTEGER *qpc_ticks);
#endif // defined(XRT_OS_WINDOWS) || defined(XRT_DOXYGEN)


#ifdef __cplusplus
}
#endif
