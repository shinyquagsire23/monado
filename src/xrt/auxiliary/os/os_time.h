// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrapper around OS native time functions.
 * @author Drew DeVault <sir@cmpwn.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_os
 */

#pragma once

#include "xrt/xrt_config.h"
#include "xrt/xrt_compiler.h"

#ifdef XRT_OS_LINUX
#include <time.h>
#include <sys/time.h>
#define XRT_HAVE_TIMESPEC
#define XRT_HAVE_TIMEVAL

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
XRT_MAYBE_UNUSED static inline void
os_nanosleep(long nsec)
{
#ifdef XRT_OS_LINUX
	struct timespec spec = {
	    .tv_sec = 0,
	    .tv_nsec = nsec,
	};
	nanosleep(&spec, NULL);
#endif
}

#ifdef XRT_HAVE_TIMESPEC
/*!
 * @brief Convert a timespec struct to nanoseconds.
 * @ingroup aux_os_time_extra
 */
XRT_MAYBE_UNUSED static inline uint64_t
os_timespec_to_ns(struct timespec *spec)
{
	uint64_t ns = 0;
	ns += (uint64_t)spec->tv_sec * 1000 * 1000 * 1000;
	ns += (uint64_t)spec->tv_nsec;
	return ns;
}
#endif // XRT_HAVE_TIMESPEC


#ifdef XRT_HAVE_TIMEVAL
/*!
 * @brief Convert a timeval struct to nanoseconds.
 * @ingroup aux_os_time_extra
 */
XRT_MAYBE_UNUSED static inline uint64_t
os_timeval_to_ns(struct timeval *val)
{
	uint64_t ns = 0;
	ns += (uint64_t)val->tv_sec * 1000 * 1000 * 1000;
	ns += (uint64_t)val->tv_usec * 1000;
	return ns;
}
#endif // XRT_HAVE_TIMEVAL


/*!
 * @brief Return a monotonic clock in nanoseconds.
 * @ingroup aux_os_time
 */
XRT_MAYBE_UNUSED static inline uint64_t
os_monotonic_get_ns(void)
{
#ifdef XRT_OS_LINUX
	struct timespec ts;
	int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret != 0) {
		return 0;
	}

	return os_timespec_to_ns(&ts);
#endif
}


#ifdef __cplusplus
}
#endif
