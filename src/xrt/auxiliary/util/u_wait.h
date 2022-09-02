// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tiny file to implement precise waiting functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_config_os.h"
#include "os/os_time.h"

#if defined(XRT_DOXYGEN)

/*!
 * OS specific tweak to wait time.
 *
 * @todo Measure on Windows.
 * @ingroup aux_util
 */
#define U_WAIT_MEASURED_SCHEDULER_LATENCY_NS (uint64_t)(0)

#elif defined(XRT_OS_LINUX) || defined(XRT_OS_ANDROID)
#define U_WAIT_MEASURED_SCHEDULER_LATENCY_NS (uint64_t)(50 * 1000)
#elif defined(XRT_OS_WINDOWS)
#define U_WAIT_MEASURED_SCHEDULER_LATENCY_NS (uint64_t)(0)
#else
#error "Unsupported platform!"
#endif


/*!
 * Waits until the given time using the @ref os_precise_sleeper.
 *
 * @ingroup aux_util
 */
static inline void
u_wait_until(struct os_precise_sleeper *sleeper, uint64_t until_ns)
{
	uint64_t now_ns = os_monotonic_get_ns();

	// Lets hope its not to late.
	bool fuzzy_in_the_past = time_is_less_then_or_within_range(until_ns, now_ns, U_TIME_1MS_IN_NS);

	// When we should wake up is in the past:ish.
	if (fuzzy_in_the_past) {
		return;
	}

	// Sufficiently in the future.
	uint32_t delay = (uint32_t)(until_ns - now_ns - U_WAIT_MEASURED_SCHEDULER_LATENCY_NS);
	os_precise_sleeper_nanosleep(sleeper, delay);
}
