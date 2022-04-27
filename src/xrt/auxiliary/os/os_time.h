// Copyright 2019-2022, Collabora, Ltd.
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
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
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
 * Return a monotonic clock in nanoseconds.
 * @ingroup aux_os_time
 */
static inline uint64_t
os_monotonic_get_ns(void);

/*!
 * Sleep the given number of nanoseconds.
 *
 * Note that on some platforms, this may be somewhat less accurate than you might want.
 * On all platforms, the system scheduler has the final say.
 *
 * @see os_precise_sleeper
 *
 * @ingroup aux_os_time
 */
static inline void
os_nanosleep(int64_t nsec);

/*!
 * A structure for storing state as needed for more precise sleeping, mostly for compositor use.
 * @ingroup aux_os_time
 */
struct os_precise_sleeper;

/*!
 * Initialize members of @ref os_precise_sleeper.
 * @public @memberof os_precise_sleeper
 */
static inline void
os_precise_sleeper_init(struct os_precise_sleeper *ops);

/*!
 * De-initialize members of @ref os_precise_sleeper, and free resources, without actually freeing the given
 * pointer.
 * @public @memberof os_precise_sleeper
 */
static inline void
os_precise_sleeper_deinit(struct os_precise_sleeper *ops);

/*!
 * Sleep the given number of nanoseconds, trying harder to be precise.
 *
 * On some platforms, there is no way to improve sleep precision easily with some OS-specific state, so we forward
 * to os_nanosleep().
 *
 * Note that on all platforms, the system scheduler has the final say.
 *
 * @public @memberof os_precise_sleeper
 */
static inline void
os_precise_sleeper_nanosleep(struct os_precise_sleeper *ops, int32_t nsec);

#if defined(XRT_HAVE_TIMESPEC) || defined(XRT_DOXYGEN)
/*!
 * Convert a timespec struct to nanoseconds.
 *
 * Note that this only does the value combining, no adjustment for epochs is performed.
 *
 * @ingroup aux_os_time_extra
 */
static inline uint64_t
os_timespec_to_ns(const struct timespec *spec);

/*!
 * Convert an nanosecond integer to a timespec struct.
 *
 * Note that this only does the value splitting, no adjustment for epochs is performed.
 * @ingroup aux_os_time_extra
 */
static inline void
os_ns_to_timespec(uint64_t ns, struct timespec *spec);
#endif

#if defined(XRT_HAVE_TIMEVAL) || defined(XRT_DOXYGEN)
/*!
 * Convert a timeval struct to nanoseconds.
 * @ingroup aux_os_time_extra
 */
static inline uint64_t
os_timeval_to_ns(struct timeval *val);
#endif

#if defined(XRT_OS_LINUX) || defined(XRT_DOXYGEN)
/*!
 * Return a realtime clock in nanoseconds (Linux-only)
 *
 * @ingroup aux_os_time_extra
 */
static inline uint64_t
os_realtime_get_ns(void);
#endif

#if defined(XRT_OS_WINDOWS) || defined(XRT_DOXYGEN)
/*!
 * @brief Return a qpc freq in nanoseconds.
 * @ingroup aux_os_time
 */
static inline int64_t
os_ns_per_qpc_tick_get();
#endif


/*
 *
 * implementations follow
 *
 */

static inline void
os_nanosleep(int64_t nsec)
{
#if defined(XRT_OS_LINUX)
	struct timespec spec;
	spec.tv_sec = (nsec / U_1_000_000_000);
	spec.tv_nsec = (nsec % U_1_000_000_000);
	nanosleep(&spec, NULL);
#elif defined(XRT_OS_WINDOWS)
	Sleep((DWORD)(nsec / U_TIME_1MS_IN_NS));
#endif
}

struct os_precise_sleeper
{
#if defined(XRT_OS_WINDOWS)
	HANDLE timer;
#else
	int unused_;
#endif
};

static inline void
os_precise_sleeper_init(struct os_precise_sleeper *ops)
{
#if defined(XRT_OS_WINDOWS)
	ops->timer = CreateWaitableTimer(NULL, TRUE, NULL);
#endif
}

static inline void
os_precise_sleeper_deinit(struct os_precise_sleeper *ops)
{
#if defined(XRT_OS_WINDOWS)
	if (ops->timer) {
		CloseHandle(ops->timer);
		ops->timer = NULL;
	}
#endif
}

static inline void
os_precise_sleeper_nanosleep(struct os_precise_sleeper *ops, int32_t nsec)
{
#if defined(XRT_OS_WINDOWS)
	if (ops->timer) {
		LARGE_INTEGER timeperiod;
		timeperiod.QuadPart = -(nsec / 100);
		if (SetWaitableTimer(ops->timer, &timeperiod, 0, NULL, NULL, FALSE)) {
			// OK we could set up the timer, now let's wait.
			WaitForSingleObject(ops->timer, INFINITE);
			return;
		}
	}
#endif
	// If we fall through from an implementation, or there's no implementation needed for a platform, we
	// delegate to the regular os_nanosleep.
	os_nanosleep(nsec);
}

#if defined(XRT_HAVE_TIMESPEC)
static inline uint64_t
os_timespec_to_ns(const struct timespec *spec)
{
	uint64_t ns = 0;
	ns += (uint64_t)spec->tv_sec * U_1_000_000_000;
	ns += (uint64_t)spec->tv_nsec;
	return ns;
}

static inline void
os_ns_to_timespec(uint64_t ns, struct timespec *spec)
{
	spec->tv_sec = (ns / U_1_000_000_000);
	spec->tv_nsec = (ns % U_1_000_000_000);
}
#endif // XRT_HAVE_TIMESPEC


#if defined(XRT_HAVE_TIMEVAL) && defined(XRT_OS_LINUX)

#define OS_NS_PER_USEC (1000)

static inline uint64_t
os_timeval_to_ns(struct timeval *val)
{
	uint64_t ns = 0;
	ns += (uint64_t)val->tv_sec * U_1_000_000_000;
	ns += (uint64_t)val->tv_usec * OS_NS_PER_USEC;
	return ns;
}
#endif // defined(XRT_HAVE_TIMEVAL) && defined(XRT_OS_LINUX)

#if defined(XRT_OS_WINDOWS)
static inline int64_t
os_ns_per_qpc_tick_get()
{
	static int64_t ns_per_qpc_tick = 0;
	if (ns_per_qpc_tick == 0) {
		// Fixed at startup, so we can cache this.
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		ns_per_qpc_tick = U_1_000_000_000 / freq.QuadPart;
	}
	return ns_per_qpc_tick;
}
#endif // defined(XRT_OS_WINDOWS)

static inline uint64_t
os_monotonic_get_ns(void)
{
#if defined(XRT_OS_LINUX)
	struct timespec ts;
	int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret != 0) {
		return 0;
	}

	return os_timespec_to_ns(&ts);
#elif defined(XRT_OS_WINDOWS)
	LARGE_INTEGER qpc;
	QueryPerformanceCounter(&qpc);
	return qpc.QuadPart * os_ns_per_qpc_tick_get();
#else
#error "need port"
#endif
}

#ifdef XRT_OS_LINUX
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
