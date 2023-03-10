// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrapper around OS native time functions.
 *
 * These should be preferred over directly using native OS time functions in
 * potentially-portable code. Additionally, in most cases these are preferred
 * over timepoints from @ref time_state for general usage in drivers, etc.
 *
 * @author Christoph Haag <christoph.haag@collabora.com>
 *
 * @ingroup aux_os
 */

#include "xrt/xrt_config_os.h"

#ifdef XRT_OS_WINDOWS

#include <inttypes.h>
#include <chrono>

extern "C" uint64_t
os_realtime_get_ns(void)
{
	auto now = std::chrono::system_clock::now();
	auto nsecs = std::chrono::time_point_cast<std::chrono::nanoseconds>(now);
	auto ret = nsecs.time_since_epoch().count();
	return ret;
}
#endif
