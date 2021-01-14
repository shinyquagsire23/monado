// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  IPC util helpers, for internal use only
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_shared
 */

#pragma once

#include <xrt/xrt_handles.h>
#include <xrt/xrt_results.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "util/u_logging.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Wrapper for a socket and flags.
 */
struct ipc_message_channel
{
	int socket_fd;
	enum u_logging_level ll;
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
 *
 * @param imc Message channel to use
 * @param[in] data Pointer to the data buffer to send. Must not be
 * null: use a filler message if necessary.
 * @param[in] size Size of data pointed-to by @p data, must be greater than 0
 *
 * @public @memberof ipc_message_channel
 */
xrt_result_t
ipc_send(struct ipc_message_channel *imc, const void *data, size_t size);

/*!
 * Receive a bare message over the IPC channel.
 *
 * There are other functions if you have handles, not just scalar/aggregate
 * data.
 *
 * @param imc Message channel to use
 * @param[out] out_data Pointer to the buffer to fill with data. Must not be
 * null.
 * @param[in] size Maximum size to read, must be greater than 0
 *
 * @public @memberof ipc_message_channel
 */
xrt_result_t
ipc_receive(struct ipc_message_channel *imc, void *out_data, size_t size);

/*!
 * @name File Descriptor utilities
 * @brief These are typically called from within the send/receive_handles
 * functions.
 * @{
 */
#ifdef XRT_OS_UNIX
/*!
 * Receive a message along with a known number of file descriptors over the IPC
 * channel.
 *
 * @param imc Message channel to use
 * @param[out] out_data Pointer to the buffer to fill with data. Must not be
 * null.
 * @param[in] size Maximum size to read, must be greater than 0
 * @param[out] out_handles Array of file descriptors to populate.  Must not be
 * null.
 * @param[in] num_handles Number of elements to receive into @p out_handles,
 * must be greater than 0 and must match the value provided at the other end.
 *
 * @public @memberof ipc_message_channel
 */
xrt_result_t
ipc_receive_fds(struct ipc_message_channel *imc, void *out_data, size_t size, int *out_handles, uint32_t num_handles);

/*!
 * Send a message along with file descriptors over the IPC channel.
 *
 * @param imc Message channel to use
 * @param[in] data Pointer to the data buffer to send. Must not be
 * null: use a filler message if necessary.
 * @param[in] size Size of data pointed-to by @p data, must be greater than 0
 * @param[out] handles Array of file descriptors to send.  Must not be
 * null.
 * @param[in] num_handles Number of elements in @p handles, must be greater
 * than 0. If this is variable, it must also be separately transmitted ahead of
 * time, because the receiver must have the same value in its receive call.
 *
 * @public @memberof ipc_message_channel
 */
xrt_result_t
ipc_send_fds(struct ipc_message_channel *imc, const void *data, size_t size, const int *handles, uint32_t num_handles);
#endif // XRT_OS_UNIX
/*!
 * @}
 */

/*!
 * @name Shared memory handle utilities
 * @brief Send/receive shared memory handles along with scalar/aggregate message
 * data.
 * @{
 */

/*!
 * Receive a message along with a known number of shared memory handles over the
 * IPC channel.
 *
 * @param imc Message channel to use
 * @param[out] out_data Pointer to the buffer to fill with data. Must not be
 * null.
 * @param[in] size Maximum size to read, must be greater than 0
 * @param[out] out_handles Array of shared memory handles to populate.  Must not
 * be null.
 * @param[in] num_handles Number of elements to receive into @p out_handles,
 * must be greater than 0 and must match the value provided at the other end.
 *
 * @public @memberof ipc_message_channel
 * @relatesalso xrt_shmem_handle_t
 */
xrt_result_t
ipc_receive_handles_shmem(struct ipc_message_channel *imc,
                          void *out_data,
                          size_t size,
                          xrt_shmem_handle_t *out_handles,
                          uint32_t num_handles);


/*!
 * Send a message along with shared memory handles over the IPC channel.
 *
 * @param imc Message channel to use
 * @param[in] data Pointer to the data buffer to send. Must not be
 * null: use a filler message if necessary.
 * @param[in] size Size of data pointed-to by @p data, must be greater than 0
 * @param[out] handles Array of shared memory handles to send.  Must not be
 * null.
 * @param[in] num_handles Number of elements in @p handles, must be greater
 * than 0. If this is variable, it must also be separately transmitted ahead of
 * time, because the receiver must have the same value in its receive call.
 *
 * @public @memberof ipc_message_channel
 * @relatesalso xrt_shmem_handle_t
 */
xrt_result_t
ipc_send_handles_shmem(struct ipc_message_channel *imc,
                       const void *data,
                       size_t size,
                       const xrt_shmem_handle_t *handles,
                       uint32_t num_handles);
/*!
 * @}
 */


/*!
 * @name Graphics buffer handle utilities
 * @brief Send/receive graphics buffer handles along with scalar/aggregate
 * message data.
 * @{
 */

/*!
 * Receive a message along with a known number of graphics buffer handles over
 * the IPC channel.
 *
 * @param imc Message channel to use
 * @param[out] out_data Pointer to the buffer to fill with data. Must not be
 * null.
 * @param[in] size Maximum size to read, must be greater than 0
 * @param[out] out_handles Array of graphics buffer handles to populate.  Must
 * not be null.
 * @param[in] num_handles Number of elements to receive into @p out_handles,
 * must be greater than 0 and must match the value provided at the other end.
 *
 * @public @memberof ipc_message_channel
 * @relatesalso xrt_graphics_buffer_handle_t
 */
xrt_result_t
ipc_receive_handles_graphics_buffer(struct ipc_message_channel *imc,
                                    void *out_data,
                                    size_t size,
                                    xrt_graphics_buffer_handle_t *out_handles,
                                    uint32_t num_handles);


/*!
 * Send a message along with native graphics buffer handles over the IPC
 * channel.
 *
 * @param imc Message channel to use
 * @param[in] data Pointer to the data buffer to send. Must not be
 * null: use a filler message if necessary.
 * @param[in] size Size of data pointed-to by @p data, must be greater than 0
 * @param[out] handles Array of graphics buffer handles to send.  Must not be
 * null.
 * @param[in] num_handles Number of elements in @p handles, must be greater
 * than 0. If this is variable, it must also be separately transmitted ahead of
 * time, because the receiver must have the same value in its receive call.
 *
 * @public @memberof ipc_message_channel
 * @relatesalso xrt_graphics_buffer_handle_t
 */
xrt_result_t
ipc_send_handles_graphics_buffer(struct ipc_message_channel *imc,
                                 const void *data,
                                 size_t size,
                                 const xrt_graphics_buffer_handle_t *handles,
                                 uint32_t num_handles);

/*!
 * @}
 */


/*!
 * @name Graphics buffer handle utilities
 * @brief Send/receive graphics buffer handles along with scalar/aggregate
 * message data.
 * @{
 */

/*!
 * Receive a message along with a known number of graphics sync handles over
 * the IPC channel.
 *
 * @param imc Message channel to use
 * @param[out] out_data Pointer to the sync to fill with data. Must not be null.
 * @param[in] size Maximum size to read, must be greater than 0
 * @param[out] out_handles Array of graphics sync handles to populate. Must not
 * be null.
 * @param[in] num_handles Number of elements to receive into @p out_handles,
 * must be greater than 0 and must match the value provided at the other end.
 *
 * @public @memberof ipc_message_channel
 * @relatesalso xrt_graphics_sync_handle_t
 */
xrt_result_t
ipc_receive_handles_graphics_sync(struct ipc_message_channel *imc,
                                  void *out_data,
                                  size_t size,
                                  xrt_graphics_sync_handle_t *out_handles,
                                  uint32_t num_handles);

/*!
 * Send a message along with native graphics sync handles over the IPC channel.
 *
 * @param imc Message channel to use
 * @param[in] data Pointer to the data sync to send. Must not be null: use a
 * filler message if necessary.
 * @param[in] size Size of data pointed-to by @p data, must be greater than 0
 * @param[out] handles Array of graphics sync handles to send.  Must not be
 * null.
 * @param[in] num_handles Number of elements in @p handles, must be greater than
 * 0. If this is variable, it must also be separately transmitted ahead of time,
 * because the receiver must have the same value in its receive call.
 *
 * @public @memberof ipc_message_channel
 * @relatesalso xrt_graphics_sync_handle_t
 */
xrt_result_t
ipc_send_handles_graphics_sync(struct ipc_message_channel *imc,
                               const void *data,
                               size_t size,
                               const xrt_graphics_sync_handle_t *handles,
                               uint32_t num_handles);

/*!
 * @}
 */

#ifdef __cplusplus
}
#endif
