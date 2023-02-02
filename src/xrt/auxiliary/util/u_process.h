// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple process handling
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct u_process;

/*!
 * Creates a handle for this process that is unique to the operating system user. Returns NULL if another process
 * holding a handle is already running.
 *
 * @todo If built without libbsd support, a placeholder value is returned that needs to be handled by the caller.
 *
 * @return a new u_process handle if no monado instance is running, NULL if another instance is already running.
 * @ingroup aux_util
 */
struct u_process *
u_process_create_if_not_running(void);

/*!
 * Releases the unique handle of the operating system user.
 *
 * @ingroup aux_util
 */
void
u_process_destroy(struct u_process *proc);

#ifdef __cplusplus
}
#endif
