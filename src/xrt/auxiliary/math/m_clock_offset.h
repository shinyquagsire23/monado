// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helpers to estimate offsets between clocks
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#include "util/u_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Helper to estimate the offset between two clocks using exponential smoothing.
 *
 * Given a sample from two timestamp domains A and B that should have been
 * sampled as close as possible, together with an estimate of the offset between
 * A clock and B clock (or zero), it applies a smoothing average on the
 * estimated offset and returns @p a in B clock.
 *
 * @param freq About how many times per second this function is called. Helps setting a good decay value.
 * @param a Timestamp in clock A of the event
 * @param b Timestamp in clock B of the event
 * @param[in,out] inout_a2b Pointer to the current offset estimate from A to B, or 0 if unknown.
 * Value pointed-to will be updated.
 * @return timepoint_ns @p a in B clock
 */
static inline timepoint_ns
m_clock_offset_a2b(float freq, timepoint_ns a, timepoint_ns b, time_duration_ns *inout_a2b)
{
	// Totally arbitrary way of computing alpha, if you have a better one, replace it
	const float alpha = 1.0 - 12.5 / freq; // Weight to put on accumulated a2b
	time_duration_ns old_a2b = *inout_a2b;
	time_duration_ns got_a2b = b - a;
	time_duration_ns new_a2b = old_a2b * alpha + got_a2b * (1.0 - alpha);
	if (old_a2b == 0) { // a2b has not been set yet
		new_a2b = got_a2b;
	}
	*inout_a2b = new_a2b;
	return a + new_a2b;
}

#ifdef __cplusplus
}
#endif
