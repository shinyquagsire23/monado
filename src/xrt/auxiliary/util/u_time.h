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

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * The number of nanoseconds in a second.
 *
 * @see timepoint_ns
 * @ingroup time_ns_to_s
 */
#define U_1_000_000_000 (1000000000)

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
 * Convert nanoseconds duration to float seconds.
 *
 * @see timepoint_ns
 * @ingroup aux_util
 */
static inline float
time_ns_to_s(time_duration_ns ns)
{
	return (float)(ns) / (float)U_1_000_000_000;
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
	return (time_duration_ns)(duration * (double)U_1_000_000_000);
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
time_state_create();


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


#ifdef __cplusplus
}
#endif
