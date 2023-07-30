// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  More-internal client side code.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_client
 */

#pragma once

#include "xrt/xrt_results.h"
#include "ipc_client.h"


/*!
 * Set up the basics of the client connection: socket and shared mem
 * @param ipc_c     Empty IPC connection struct
 * @param log_level Log level for IPC messages
 * @param i_info    Instance info to send to server
 * @return XRT_SUCCESS on success
 *
 * @ingroup ipc_client
 */
xrt_result_t
ipc_client_connection_init(struct ipc_connection *ipc_c,
                           enum u_logging_level log_level,
                           struct xrt_instance_info *i_info);


/*!
 * Tear down the basics of the client connection: socket and shared mem
 * @param ipc_c initialized IPC connection struct
 *
 * @ingroup ipc_client
 */
void
ipc_client_connection_fini(struct ipc_connection *ipc_c);
