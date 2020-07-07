// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common client side code.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_client
 */

#include "ipc_client.h"
#include "ipc_utils.h"

#include "util/u_misc.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>


xrt_result_t
ipc_client_send_and_get_reply(struct ipc_connection *ipc_c,
                              const void *msg_ptr,
                              size_t msg_size,
                              void *reply_ptr,
                              size_t reply_size)
{
	// Other threads must not read/write the fd while we wait for reply
	os_mutex_lock(&ipc_c->mutex);

	xrt_result_t result = ipc_send(&ipc_c->imc, msg_ptr, msg_size);
	if (result != XRT_SUCCESS) {
		os_mutex_unlock(&ipc_c->mutex);
		return result;
	}

	result = ipc_receive(&ipc_c->imc, reply_ptr, reply_size);
	os_mutex_unlock(&ipc_c->mutex);
	return result;
}

xrt_result_t
ipc_client_send_and_get_reply_fds(struct ipc_connection *ipc_c,
                                  const void *msg_ptr,
                                  size_t msg_size,
                                  void *reply_ptr,
                                  size_t reply_size,
                                  int *fds,
                                  size_t num_fds)
{
	// Other threads must not read/write the fd while we wait for reply
	os_mutex_lock(&ipc_c->mutex);

	xrt_result_t result = ipc_send(&ipc_c->imc, msg_ptr, msg_size);
	if (result != XRT_SUCCESS) {
		os_mutex_unlock(&ipc_c->mutex);
		return result;
	}

	result =
	    ipc_receive_fds(&ipc_c->imc, reply_ptr, reply_size, fds, num_fds);

	os_mutex_unlock(&ipc_c->mutex);
	return result;
}

xrt_result_t
ipc_client_send_message(struct ipc_connection *ipc_c,
                        void *message,
                        size_t size)
{
	return ipc_client_send_and_get_reply(ipc_c, message, size, message,
	                                     size);
}
