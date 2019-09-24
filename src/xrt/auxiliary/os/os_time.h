// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrapper around OS native time functions.
 * @author Drew DeVault <sir@cmpwn.com>
 *
 * @ingroup aux_os
 */

#pragma once

#include "xrt/xrt_config.h"
#include "xrt/xrt_compiler.h"

#ifdef XRT_OS_LINUX
#include <time.h>
#else
#error "No time support on non-Linux platforms yet."
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Write an output report to the given device.
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

#ifdef __cplusplus
}
#endif
