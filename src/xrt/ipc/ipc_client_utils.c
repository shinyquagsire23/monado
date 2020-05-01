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

#include "util/u_misc.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>


ipc_result_t
ipc_client_send_and_get_reply(struct ipc_connection *ipc_c,
                              void *msg_ptr,
                              size_t msg_size,
                              void *reply_ptr,
                              size_t reply_size)
{
	if (ipc_c->socket_fd < 0) {
		IPC_ERROR(ipc_c, "Error sending - not connected!");
		return IPC_FAILURE;
	}

	ssize_t len = send(ipc_c->socket_fd, msg_ptr, msg_size, 0);
	if ((size_t)len != msg_size) {
		IPC_ERROR(ipc_c, "Error sending - cannot continue!");
		return IPC_FAILURE;
	}


	// wait for the response
	struct iovec iov = {0};
	struct msghdr msg = {0};

	iov.iov_base = reply_ptr;
	iov.iov_len = reply_size;

	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	len = recvmsg(ipc_c->socket_fd, &msg, 0);
	if (len < 0) {
		IPC_ERROR(ipc_c, "recvmsg failed with error: %s",
		          strerror(errno));
		return IPC_FAILURE;
	}

	if ((size_t)len != reply_size) {
		IPC_ERROR(ipc_c, "recvmsg failed with error: wrong size");
		return IPC_FAILURE;
	}

	return IPC_SUCCESS;
}

ipc_result_t
ipc_client_send_and_get_reply_fds(ipc_connection_t *ipc_c,
                                  void *msg_ptr,
                                  size_t msg_size,
                                  void *reply_ptr,
                                  size_t reply_size,
                                  int *fds,
                                  size_t num_fds)
{
	if (send(ipc_c->socket_fd, msg_ptr, msg_size, 0) == -1) {
		IPC_ERROR(ipc_c, "Error sending - cannot continue!");
		return IPC_FAILURE;
	}

	const size_t fds_size = sizeof(int) * num_fds;
	char buf[CMSG_SPACE(fds_size)];
	memset(buf, 0, sizeof(buf));

	struct iovec iov = {0};
	iov.iov_base = reply_ptr;
	iov.iov_len = reply_size;

	struct msghdr msg = {0};
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);

	ssize_t len = recvmsg(ipc_c->socket_fd, &msg, 0);

	if (len < 0) {
		IPC_ERROR(ipc_c, "recvmsg failed with error: %s",
		          strerror(errno));
		return -1;
	}

	if (len == 0) {
		IPC_ERROR(ipc_c, "recvmsg failed with error: no data");
		return -1;
	}

	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	memcpy(fds, (int *)CMSG_DATA(cmsg), fds_size);

	return IPC_SUCCESS;
}

ipc_result_t
ipc_client_send_message(ipc_connection_t *ipc_c, void *message, size_t size)
{
	return ipc_client_send_and_get_reply(ipc_c, message, size, message,
	                                     size);
}
