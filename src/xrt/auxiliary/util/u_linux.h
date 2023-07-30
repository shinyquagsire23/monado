// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Various helpers for doing Linux specific things.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_windows.h"
#include "util/u_logging.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Try to set realtime priority on this thread. Passing in log_level to control
 * how chatty this function is, the name is to make the logging pretty, can be
 * NULL and the code will try to figure out the name itself.
 *
 * @param name      Thread name to be used in logging.
 * @param log_level Logging level to control chattiness.
 *
 * @aux_util
 */
void
u_linux_try_to_set_realtime_priority_on_thread(enum u_logging_level log_level, const char *name);


#ifdef __cplusplus
}
#endif
