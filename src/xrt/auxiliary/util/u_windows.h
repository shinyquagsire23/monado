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


#ifdef __cplusplus
}
#endif
