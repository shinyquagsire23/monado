// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common client side code.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_client
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "ipc_protocol.h"

#include "util/u_threading.h"

#include <stdio.h>


/*
 *
 * Logging
 *
 */

/*!
 * Spew level logging.
 */
#define IPC_SPEW(c, ...)                                                       \
	do {                                                                   \
		if ((c)->print_spew) {                                         \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

/*!
 * Debug level logging.
 */
#define IPC_DEBUG(c, ...)                                                      \
	do {                                                                   \
		if ((c)->print_debug) {                                        \
			fprintf(stderr, "%s - ", __func__);                    \
			fprintf(stderr, __VA_ARGS__);                          \
			fprintf(stderr, "\n");                                 \
		}                                                              \
	} while (false)

/*!
 * Error level logging.
 */
#define IPC_ERROR(c, ...)                                                      \
	do {                                                                   \
		(void)(c)->print_debug;                                        \
		fprintf(stderr, "%s - ", __func__);                            \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, "\n");                                         \
	} while (false)


/*
 *
 * Structs
 *
 */

struct xrt_compositor_fd;


/*!
 * Connection.
 */
typedef struct ipc_connection
{
	int socket_fd;

	struct ipc_shared_memory *ism;
	int ism_fd;

	struct os_mutex mutex;

	bool print_debug; // TODO: link to settings
	bool print_spew;  // TODO: link to settings

} ipc_connection_t;


ipc_result_t
ipc_client_send_message(ipc_connection_t *ipc_c, void *message, size_t size);

ipc_result_t
ipc_client_send_and_get_reply(struct ipc_connection *ipc_c,
                              void *msg_ptr,
                              size_t msg_size,
                              void *reply_ptr,
                              size_t reply_size);

ipc_result_t
ipc_client_send_and_get_reply_fds(ipc_connection_t *ipc_c,
                                  void *msg_ptr,
                                  size_t msg_size,
                                  void *reply_ptr,
                                  size_t reply_size,
                                  int *fds,
                                  size_t num_fds);


/*
 *
 * Internal functions.
 *
 */

int
ipc_client_compositor_create(ipc_connection_t *ipc_c,
                             struct xrt_device *xdev,
                             bool flip_y,
                             struct xrt_compositor_fd **out_xcfd);

struct xrt_device *
ipc_client_hmd_create(ipc_connection_t *ipc_c, uint32_t device_id);

struct xrt_device *
ipc_client_device_create(ipc_connection_t *ipc_c, uint32_t device_id);
