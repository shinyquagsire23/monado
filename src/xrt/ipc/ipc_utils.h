// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  IPC util helpers, for internal use only
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_internal
 */

#pragma once

#include <xrt/xrt_handles.h>
#include <xrt/xrt_results.h>

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Wrapper for a socket and flags.
 */
struct ipc_message_channel
{
	int socket_fd;
	bool print_debug;
};

/*!
 * Close an IPC message channel
 */
void
ipc_message_channel_close(struct ipc_message_channel *imc);

/*!
 * Send a bare message over the IPC channel.
 *
 * There are other functions if you have handles, not just scalar/aggregate
 * data.
 */
xrt_result_t
ipc_send(struct ipc_message_channel *imc, const void *data, size_t size);

/*!
 * Receive a bare message over the IPC channel.
 *
 * There are other functions if you have handles, not just scalar/aggregate
 * data.
 */
xrt_result_t
ipc_receive(struct ipc_message_channel *imc, void *out_data, size_t size);

/*!
 * @name File Descriptor utilities
 * @brief These are typically called from within the send/receive_handles
 * functions.
 * @{
 */
/*!
 * Receive a message along with a known number of file descriptors over the IPC
 * channel.
 */
xrt_result_t
ipc_receive_fds(struct ipc_message_channel *imc,
                void *out_data,
                size_t size,
                int *out_handles,
                size_t num_handles);

/*!
 * Send a message along with file descriptors over the IPC channel.
 */
xrt_result_t
ipc_send_fds(struct ipc_message_channel *imc,
             const void *data,
             size_t size,
             const int *handles,
             size_t num_handles);
/*!
 * @}
 */

/*!
 * @name Shared memory handle utilities
 * @brief Send/receive shared memory handles along with scalar/aggregate message
 * data.
 * @{
 */
xrt_result_t
ipc_receive_handles_shmem(struct ipc_message_channel *imc,
                          void *out_data,
                          size_t size,
                          xrt_shmem_handle_t *out_handles,
                          size_t num_handles);


xrt_result_t
ipc_send_handles_shmem(struct ipc_message_channel *imc,
                       const void *data,
                       size_t size,
                       const xrt_shmem_handle_t *handles,
                       size_t num_handles);
/*!
 * @}
 */

/*!
 * @name Graphics buffer handle utilities
 * @brief Send/receive graphics buffer handles along with scalar/aggregate
 * message data.
 * @{
 */
xrt_result_t
ipc_receive_handles_graphics_buffer(struct ipc_message_channel *imc,
                                    void *out_data,
                                    size_t size,
                                    xrt_graphics_buffer_handle_t *out_handles,
                                    size_t num_handles);


xrt_result_t
ipc_send_handles_graphics_buffer(struct ipc_message_channel *imc,
                                 const void *data,
                                 size_t size,
                                 const xrt_graphics_buffer_handle_t *handles,
                                 size_t num_handles);
/*!
 * @}
 */

#ifdef __cplusplus
}
#endif
