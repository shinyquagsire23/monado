// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Server util helpers.
 * @author Pete Black <pblack@collabora.com>
 * @ingroup ipc_server
 */

#pragma once

#include "ipc_protocol.h"

#include <xrt/xrt_results.h>


xrt_result_t
ipc_reply(int socket, void *data, size_t len);
xrt_result_t
ipc_reply_fds(int socket, void *data, size_t size, int *fds, uint32_t num_fds);
