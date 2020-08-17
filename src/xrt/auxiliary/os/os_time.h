// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrapper around OS native time functions.
 *
 * These should be preferred over directly using native OS time functions in
 * potentially-portable code. Additionally, in most cases these are preferred
 * over timepoints from @ref time_state for general usage in drivers, etc.
 *
 * @author Drew DeVault <sir@cmpwn.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_os
 */

#pragma once

#include "xrt/xrt_config_os.h"
#include "xrt/xrt_compiler.h"

#include "util/u_time.h"

#ifdef XRT_OS_LINUX
#include <time.h>
#include <sys/time.h>
#define XRT_HAVE_TIMESPEC
#define XRT_HAVE_TIMEVAL

#elif defined(XRT_OS_WINDOWS)
#include <time.h>
#define XRT_HAVE_TIMESPEC

#elif defined(XRT_DOXYGEN)
#include <time.h>
#define XRT_HAVE_TIMESPEC
#define XRT_HAVE_TIMEVAL

#else
#error "No time support on non-Linux platforms yet."
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup aux_os_time Portable Timekeeping
 * @ingroup aux_os
 *
 * @brief Unifying wrapper around system time retrieval functions.
 */


/*!
 * @defgroup aux_os_time_extra Extra Timekeeping Utilities
 * @ingroup aux_os_time
 *
 * @brief Less-portable utility functions for manipulating system time, for
 * interoperation with platform APIs.
 */


/*!
 * @brief Sleep the given number of nanoseconds.
 * @ingroup aux_os_time
 */
static inline void
os_nanosleep(long nsec)
{
#if defined(XRT_OS_LINUX)
	struct timespec spec;
	spec.tv_sec = (nsec / U_1_000_000_000);
	spec.tv_nsec = (nsec % U_1_000_000_000);
	nanosleep(&spec, NULL);
#elif defined(XRT_OS_WINDOWS)
	Sleep(nsec / 1000);
#endif
}

#ifdef XRT_HAVE_TIMESPEC
/*!
 * @brief Convert a timespec struct to nanoseconds.
 * @ingroup aux_os_time_extra
 */
static inline uint64_t
os_timespec_to_ns(const struct timespec *spec)
{
	uint64_t ns = 0;
	ns += (uint64_t)spec->tv_sec * U_1_000_000_000;
	ns += (uint64_t)spec->tv_nsec;
	return ns;
}

/*!
 * @brief Convert an nanosecond integer to a timespec struct.
 * @ingroup aux_os_time_extra
 */
static inline void
os_ns_to_timespec(uint64_t ns, struct timespec *spec)
{
	spec->tv_sec = (ns / U_1_000_000_000);
	spec->tv_nsec = (ns % U_1_000_000_000);
}
#endif // XRT_HAVE_TIMESPEC


#ifdef XRT_HAVE_TIMEVAL
/*!
 * @brief Convert a timeval struct to nanoseconds.
 * @ingroup aux_os_time_extra
 */
static inline uint64_t
os_timeval_to_ns(struct timeval *val)
{
	uint64_t ns = 0;
	ns += (uint64_t)val->tv_sec * U_1_000_000_000;
#define OS_NS_PER_USEC (1000)
	ns += (uint64_t)val->tv_usec * OS_NS_PER_USEC;
	return ns;
}
#endif // XRT_HAVE_TIMEVAL

#ifdef XRT_OS_WINDOWS
#define CLOCK_MONOTONIC 0
#define CLOCK_REALTIME 1

static int
clock_gettime(int clk_id, struct timespec *spec)
{
	__int64 wintime;

	//! @todo We should be using QueryPerformanceCounter
	GetSystemTimeAsFileTime((FILETIME *)&wintime);
	// 1jan1601 to 1jan1970
	wintime -= 116444736000000000i64;
	// seconds
	spec->tv_sec = wintime / 10000000i64;
	// nano-seconds
	spec->tv_nsec = wintime % 10000000i64 * 100;

	return 0;
}

#endif // XRT_OS_WINDOWS
/*!
 * @brief Return a monotonic clock in nanoseconds.
 * @ingroup aux_os_time
 */
static inline uint64_t
os_monotonic_get_ns(void)
{
#if defined(XRT_OS_LINUX) || defined(XRT_OS_WINDOWS)
	struct timespec ts;
	int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret != 0) {
		return 0;
	}

	return os_timespec_to_ns(&ts);
#endif
}

#ifdef XRT_OS_LINUX
/*!
 * @brief Return a realtime clock in nanoseconds.
 * @ingroup aux_os_time
 */
static inline uint64_t
os_realtime_get_ns(void)
{
	struct timespec ts;
	int ret = clock_gettime(CLOCK_REALTIME, &ts);
	if (ret != 0) {
		return 0;
	}

	return os_timespec_to_ns(&ts);
}
#endif


#ifdef __cplusplus
}
#endif
