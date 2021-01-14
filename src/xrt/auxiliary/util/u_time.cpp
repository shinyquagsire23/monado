// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of a steady, convertible clock.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#include "xrt/xrt_config_os.h"
#include "xrt/xrt_compiler.h"

#include "os/os_time.h"

#include "u_time.h"

#include <new>
#include <assert.h>
#include <stdlib.h>
#include <time.h>


/*
 *
 * Structs.
 *
 */

struct time_state
{
	timepoint_ns offset;
};


/*
 *
 * 'Exported' functions.
 *
 */

extern "C" struct time_state *
time_state_create()
{
	time_state *state = new (std::nothrow) time_state;
	state->offset = os_monotonic_get_ns();
	return state;
}

extern "C" void
time_state_destroy(struct time_state **state_ptr)
{
	struct time_state *state = *state_ptr;

	if (state == NULL) {
		return;
	}

	delete state;
	*state_ptr = NULL;
}

extern "C" timepoint_ns
time_state_get_now(struct time_state const *state)
{
	assert(state != NULL);

	return time_state_monotonic_to_ts_ns(state, os_monotonic_get_ns());
}

extern "C" timepoint_ns
time_state_get_now_and_update(struct time_state *state)
{
	assert(state != NULL);

	return time_state_get_now(state);
}

extern "C" void
time_state_to_timespec(struct time_state const *state, timepoint_ns timestamp, struct timespec *out)
{
	assert(state != NULL);
	assert(out != NULL);

	uint64_t ns = time_state_ts_to_monotonic_ns(state, timestamp);

	out->tv_sec = ns / (U_1_000_000_000);
	out->tv_nsec = ns % (U_1_000_000_000);
}

extern "C" timepoint_ns
time_state_from_timespec(struct time_state const *state, const struct timespec *timespecTime)
{
	assert(state != NULL);
	assert(timespecTime != NULL);

	uint64_t ns = 0;
	ns += timespecTime->tv_nsec;
	ns += timespecTime->tv_sec * U_1_000_000_000;

	return time_state_monotonic_to_ts_ns(state, ns);
}

extern "C" timepoint_ns
time_state_monotonic_to_ts_ns(struct time_state const *state, uint64_t monotonic_ns)
{
	assert(state != NULL);

	return monotonic_ns - state->offset;
}

extern "C" uint64_t
time_state_ts_to_monotonic_ns(struct time_state const *state, timepoint_ns timestamp)
{
	assert(state != NULL);

	return timestamp + state->offset;
}
