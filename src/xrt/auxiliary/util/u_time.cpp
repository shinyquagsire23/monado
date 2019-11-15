// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of a steady, convertible clock.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#include "u_time.h"

#include <chrono>
#include <new>
#include <assert.h>
#include <stdlib.h>

using namespace std::chrono;

struct MatchingTimePoints
{
	system_clock::time_point sys = system_clock::now();
	steady_clock::time_point steady = steady_clock::now();
	// high_resolution_clock::time_point highRes;

	timepoint_ns
	getTimestamp(time_state const &prevState);
};

struct time_state
{
	MatchingTimePoints lastTimePoints = {};
	timepoint_ns lastTime = {};
};

timepoint_ns
MatchingTimePoints::getTimestamp(time_state const &prevState)
{
	//! @todo right now just doing steady clock for simplicity.
	//! @todo Eventually need to make the high-res clock steady.
	auto elapsed = steady - prevState.lastTimePoints.steady;
	return prevState.lastTime + duration_cast<nanoseconds>(elapsed).count();
}

extern "C" struct time_state *
time_state_create()
{

	time_state *state = new (std::nothrow) time_state;
	return state;
}

extern "C" void
time_state_destroy(struct time_state **state_ptr)
{
	delete state;
}

extern "C" timepoint_ns
time_state_get_now(struct time_state const *state)
{
	assert(state != NULL);
	auto now = MatchingTimePoints();

	return now.getTimestamp(*state);
}

extern "C" timepoint_ns
time_state_get_now_and_update(struct time_state *state)
{
	assert(state != NULL);
	auto now = MatchingTimePoints();

	auto timestamp = now.getTimestamp(*state);

	// Update the state
	state->lastTimePoints = now;
	state->lastTime = timestamp;

	return timestamp;
}

extern "C" void
time_state_to_timespec(struct time_state const *state,
                       timepoint_ns timestamp,
                       struct timespec *out)
{
	assert(state != NULL);
	assert(out != NULL);
	// Subject to some jitter, roughly up to the magnitude of the actual
	// resolution difference between the used time source and the system
	// clock, as well as the non-simultaneity of the calls in
	// MatchingTimePoints::getNow()
	auto sinceLastUpdate = nanoseconds{timestamp - state->lastTime};
	auto equivSystemTimepoint = state->lastTimePoints.sys + sinceLastUpdate;

	// System clock epoch is same as unix epoch for all known
	// implementations, but not strictly standard-required yet, see
	// wg21.link/p0355
	auto sinceEpoch = equivSystemTimepoint.time_since_epoch();
	auto secondsSinceEpoch = duration_cast<seconds>(sinceEpoch);
	sinceEpoch -= secondsSinceEpoch;
	out->tv_sec = secondsSinceEpoch.count();
	out->tv_nsec = duration_cast<nanoseconds>(sinceEpoch).count();
}


extern "C" timepoint_ns
time_state_from_timespec(struct time_state const *state,
                         const struct timespec *timespecTime)
{
	assert(state != NULL);
	assert(timespecTime != NULL);
	auto sinceEpoch =
	    seconds{timespecTime->tv_sec} + nanoseconds{timespecTime->tv_nsec};


	// System clock epoch is same as unix epoch for all known
	// implementations, but not strictly standard-required yet, see
	// wg21.link/p0355
	auto systemTimePoint = time_point<system_clock, nanoseconds>{
	    duration_cast<system_clock::duration>(sinceEpoch)};

	// duration between last update and the supplied timespec
	auto sinceLastUpdate = state->lastTimePoints.sys - systemTimePoint;

	// Offset the last timestamp by that duration.
	return state->lastTime +
	       duration_cast<nanoseconds>(sinceLastUpdate).count();
}