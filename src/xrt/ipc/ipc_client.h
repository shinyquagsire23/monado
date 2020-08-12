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
#include "ipc_utils.h"

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

struct xrt_compositor_native;


/*!
 * Connection.
 */
struct ipc_connection
{
	struct ipc_message_channel imc;

	struct ipc_shared_memory *ism;
	xrt_shmem_handle_t ism_handle;

	struct os_mutex mutex;

	bool print_debug; // TODO: link to settings
	bool print_spew;  // TODO: link to settings
};

/*
 *
 * Internal functions.
 *
 */

int
ipc_client_compositor_create(struct ipc_connection *ipc_c,
                             struct xrt_image_native_allocator *xina,
                             struct xrt_device *xdev,
                             struct xrt_compositor_native **out_xcn);

struct xrt_device *
ipc_client_hmd_create(struct ipc_connection *ipc_c,
                      struct xrt_tracking_origin *xtrack,
                      uint32_t device_id);

struct xrt_device *
ipc_client_device_create(struct ipc_connection *ipc_c,
                         struct xrt_tracking_origin *xtrack,
                         uint32_t device_id);
