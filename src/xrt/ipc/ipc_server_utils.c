// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Server helper functions.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_server
 */

#include "ipc_server_utils.h"

#include "util/u_misc.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>


/*
 *
 * Actual senders
 *
 */

int
ipc_reply(int socket, void *data, size_t len)
{
	struct msghdr msg = {0};
	struct iovec iov = {0};

	iov.iov_base = data;
	iov.iov_len = len;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	ssize_t ret = sendmsg(socket, &msg, MSG_NOSIGNAL);
	if (ret < 0) {
		fprintf(stderr,
		        "ERROR: Sending plain message on socket %d failed with "
		        "error: '%i' '%s'\n",
		        socket, errno, strerror(errno));
	}

	return ret;
}

int
ipc_reply_fds(int socket, void *data, size_t size, int *fds, uint32_t num_fds)
{
	union {
		uint8_t buf[512];
		struct cmsghdr align;
	} u;
	size_t cmsg_size = CMSG_SPACE(sizeof(int) * num_fds);

	struct iovec iov = {0};
	iov.iov_base = data;
	iov.iov_len = size;

	struct msghdr msg = {0};
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;
	msg.msg_control = u.buf;
	msg.msg_controllen = cmsg_size;

	const size_t fds_size = sizeof(int) * num_fds;
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(fds_size);

	memcpy(CMSG_DATA(cmsg), fds, fds_size);

	ssize_t ret = sendmsg(socket, &msg, MSG_NOSIGNAL);
	if (ret < 0) {
		fprintf(stderr,
		        "ERROR: sending %d FDs on socket %d failed with error: "
		        "'%i' '%s'\n",
		        num_fds, socket, errno, strerror(errno));
		for (uint32_t i = 0; i < num_fds; i++) {
			fprintf(stderr, "\tfd #%i: %i\n", i, fds[i]);
		}
	}

	return ret;
}
