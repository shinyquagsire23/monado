// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  IPC util helpers, for internal use only
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_internal
 */

#include "ipc_utils.h"
#include "ipc_protocol.h"

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>

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

void
ipc_message_channel_close(struct ipc_message_channel *imc)
{
	if (imc->socket_fd < 0) {
		return;
	}
	close(imc->socket_fd);
	imc->socket_fd = -1;
}

xrt_result_t
ipc_send(struct ipc_message_channel *imc, const void *data, size_t size)
{
	struct msghdr msg = {0};
	struct iovec iov = {0};

	iov.iov_base = (void *)data;
	iov.iov_len = size;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	ssize_t ret = sendmsg(imc->socket_fd, &msg, MSG_NOSIGNAL);
	if (ret < 0) {
		int code = errno;
		IPC_ERROR(
		    imc,
		    "ERROR: Sending plain message on socket %d failed with "
		    "error: '%i' '%s'",
		    (int)imc->socket_fd, code, strerror(code));
		return XRT_ERROR_IPC_FAILURE;
	}

	return XRT_SUCCESS;
}

xrt_result_t
ipc_receive(struct ipc_message_channel *imc, void *out_data, size_t size)
{

	// wait for the response
	struct iovec iov = {0};
	struct msghdr msg = {0};

	iov.iov_base = out_data;
	iov.iov_len = size;

	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;

	ssize_t len = recvmsg(imc->socket_fd, &msg, MSG_NOSIGNAL);

	if (len < 0) {
		int code = errno;
		IPC_ERROR(
		    imc,
		    "ERROR: Receiving plain message on socket %d failed with "
		    "error: '%i' '%s'",
		    (int)imc->socket_fd, code, strerror(code));
		return XRT_ERROR_IPC_FAILURE;
	}

	if ((size_t)len != size) {
		IPC_ERROR(
		    imc,
		    "recvmsg failed with error: wrong size %i, expected %i",
		    (int)len, (int)size);
		return XRT_ERROR_IPC_FAILURE;
	}

	return XRT_SUCCESS;
}

union imcontrol_buf {
	uint8_t buf[512];
	struct cmsghdr align;
};

xrt_result_t
ipc_receive_fds(struct ipc_message_channel *imc,
                void *out_data,
                size_t size,
                int *out_handles,
                size_t num_handles)
{
	union imcontrol_buf u;
	const size_t fds_size = sizeof(int) * num_handles;
	const size_t cmsg_size = CMSG_SPACE(fds_size);
	memset(u.buf, 0, cmsg_size);

	struct iovec iov = {0};
	iov.iov_base = out_data;
	iov.iov_len = size;

	struct msghdr msg = {0};
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = u.buf;
	msg.msg_controllen = cmsg_size;

	ssize_t len = recvmsg(imc->socket_fd, &msg, MSG_NOSIGNAL);
	if (len < 0) {
		IPC_ERROR(imc, "recvmsg failed with error: %s",
		          strerror(errno));
		return XRT_ERROR_IPC_FAILURE;
	}

	if (len == 0) {
		IPC_ERROR(imc, "recvmsg failed with error: no data");
		return XRT_ERROR_IPC_FAILURE;
	}
	// Did the other side actually send file descriptors.
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg == NULL) {
		return XRT_SUCCESS;
	}

	memcpy(out_handles, (int *)CMSG_DATA(cmsg), fds_size);
	return XRT_SUCCESS;
}

xrt_result_t
ipc_send_fds(struct ipc_message_channel *imc,
             const void *data,
             size_t size,
             const int *handles,
             size_t num_handles)
{
	union imcontrol_buf u;
	size_t cmsg_size = CMSG_SPACE(sizeof(int) * num_handles);

	struct iovec iov = {0};
	iov.iov_base = (void *)data;
	iov.iov_len = size;

	struct msghdr msg = {0};
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;
	msg.msg_control = u.buf;
	msg.msg_controllen = cmsg_size;

	const size_t fds_size = sizeof(int) * num_handles;
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(fds_size);

	memcpy(CMSG_DATA(cmsg), handles, fds_size);

	ssize_t ret = sendmsg(imc->socket_fd, &msg, MSG_NOSIGNAL);
	if (ret < 0) {
		IPC_ERROR(
		    imc,
		    "ERROR: sending %d FDs on socket %d failed with error: "
		    "'%i' '%s'",
		    (int)num_handles, imc->socket_fd, errno, strerror(errno));
		for (uint32_t i = 0; i < num_handles; i++) {
			IPC_ERROR(imc, "\tfd #%i: %i", i, handles[i]);
		}
		return XRT_ERROR_IPC_FAILURE;
	}
	return XRT_SUCCESS;
}

xrt_result_t
ipc_receive_handles_shmem(struct ipc_message_channel *imc,
                          void *out_data,
                          size_t size,
                          xrt_shmem_handle_t *out_handles,
                          size_t num_handles)
{
	return ipc_receive_fds(imc, out_data, size, out_handles, num_handles);
}


xrt_result_t
ipc_send_handles_shmem(struct ipc_message_channel *imc,
                       const void *data,
                       size_t size,
                       const xrt_shmem_handle_t *handles,
                       size_t num_handles)
{
	return ipc_send_fds(imc, data, size, handles, num_handles);
}

#if defined(XRT_OS_ANDROID)

#include <android/hardware_buffer.h>

#if __ANDROID_API__ < 26
#error "Android API level 26 or higher needed for AHardwareBuffer"
#endif

xrt_result_t
ipc_receive_handles_graphics_buffer(struct ipc_message_channel *imc,
                                    void *out_data,
                                    size_t size,
                                    xrt_graphics_buffer_handle_t *out_handles,
                                    uint32_t num_handles)
{
	xrt_result_t result = ipc_receive(imc, out_data, size);
	if (result != XRT_SUCCESS) {
		return result;
	}
	bool failed = false;
	for (uint32_t i = 0; i < num_handles; ++i) {
		int err = AHardwareBuffer_recvHandleFromUnixSocket(
		    imc->socket_fd, &(out_handles[i]));
		if (err != 0) {
			failed = true;
		}
	}
	return failed ? XRT_ERROR_IPC_FAILURE : XRT_SUCCESS;
}


xrt_result_t
ipc_send_handles_graphics_buffer(struct ipc_message_channel *imc,
                                 const void *data,
                                 size_t size,
                                 const xrt_graphics_buffer_handle_t *handles,
                                 uint32_t num_handles)
{
	xrt_result_t result = ipc_send(imc, data, size);
	if (result != XRT_SUCCESS) {
		return result;
	}
	bool failed = false;
	for (uint32_t i = 0; i < num_handles; ++i) {
		int err = AHardwareBuffer_sendHandleToUnixSocket(
		    handles[i], imc->socket_fd);
		if (err != 0) {
			failed = true;
		}
	}
	return failed ? XRT_ERROR_IPC_FAILURE : XRT_SUCCESS;
}

#else

xrt_result_t
ipc_receive_handles_graphics_buffer(struct ipc_message_channel *imc,
                                    void *out_data,
                                    size_t size,
                                    xrt_graphics_buffer_handle_t *out_handles,
                                    size_t num_handles)
{
	return ipc_receive_fds(imc, out_data, size, out_handles, num_handles);
}


xrt_result_t
ipc_send_handles_graphics_buffer(struct ipc_message_channel *imc,
                                 const void *data,
                                 size_t size,
                                 const xrt_graphics_buffer_handle_t *handles,
                                 size_t num_handles)
{
	return ipc_send_fds(imc, data, size, handles, num_handles);
}

#endif
