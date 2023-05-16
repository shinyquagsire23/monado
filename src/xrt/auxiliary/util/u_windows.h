// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Various helpers for doing Windows specific things.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_os
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_windows.h"
#include "util/u_logging.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * This function formats a Windows error number, as returned by `GetLastError`,
 * and writes it into the given buffer.
 *
 * @param buffer     Buffer to format the error into.
 * @param size       Size of the given buffer.
 * @param err        Error number to format a string for.
 * @param remove_end Removes and trailing `\n`, `\r` and `.` characters.
 */
const char *
u_winerror(char *buffer, size_t size, DWORD err, bool remove_end);

/*!
 * Tries to grant the 'SeIncreaseBasePriorityPrivilege' privilege to this
 * process. It is needed for HIGH and REALTIME priority Vulkan queues on NVIDIA.
 *
 * @param log_level Control the amount of logging this function does.
 */
bool
u_win_grant_inc_base_priorty_base_privileges(enum u_logging_level log_level);

/*!
 * Tries to raise the CPU priority of the process as high as possible. Returns
 * false if it could not raise the priority at all. Normal processes can raise
 * themselves from NORMAL to HIGH, while REALTIME requires either administrator
 * privileges or the 'SeIncreaseBasePriorityPrivilege' privilege to be granted.
 *
 * @param log_level Control the amount of logging this function does.
 */
bool
u_win_raise_cpu_priority(enum u_logging_level log_level);

/*!
 * Small helper function that checks process arguments for which to try.
 *
 * The parsing is really simplistic and only looks at the first argument for the
 * values `nothing`, `priority`, `privilege`, `both`. No argument at all implies
 * the value `both` making the function try to set both.
 *
 * @param log_level Control the amount of logging this function does.
 * @param argc      Number of arguments, as passed into main.
 * @param argv      Array of argument strings, as passed into main.
 */
void
u_win_try_privilege_or_priority_from_args(enum u_logging_level log_level, int argc, char *argv[]);


#ifdef __cplusplus
}
#endif
