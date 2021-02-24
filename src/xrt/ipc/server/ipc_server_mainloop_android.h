// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Additional server entry points needed for Android.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_server
 */

#pragma once

#include "ipc_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Pass an fd for a new client to the mainloop.
 *
 * @see ipc_design
 * @public @memberof ipc_server_mainloop
 */
int
ipc_server_mainloop_add_fd(struct ipc_server *vs, struct ipc_server_mainloop *ml, int newfd);


#ifdef __cplusplus
}
#endif
