// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Per client thread listening on the socket.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_server
 */

#include "util/u_misc.h"
#include "util/u_trace_marker.h"

#include "server/ipc_server.h"
#include "ipc_server_generated.h"

#ifndef XRT_OS_WINDOWS

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>


/*
 *
 * Helper functions.
 *
 */

static int
setup_epoll(volatile struct ipc_client_state *ics)
{
	int listen_socket = ics->imc.ipc_handle;
	assert(listen_socket >= 0);

	int ret = epoll_create1(EPOLL_CLOEXEC);
	if (ret < 0) {
		return ret;
	}

	int epoll_fd = ret;

	struct epoll_event ev = XRT_STRUCT_INIT;

	ev.events = EPOLLIN;
	ev.data.fd = listen_socket;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_socket, &ev);
	if (ret < 0) {
		IPC_ERROR(ics->server, "Error epoll_ctl(listen_socket) failed '%i'.", ret);
		return ret;
	}

	return epoll_fd;
}


/*
 *
 * Client loop.
 *
 */

static void
client_loop(volatile struct ipc_client_state *ics)
{
	U_TRACE_SET_THREAD_NAME("IPC Client");

	IPC_INFO(ics->server, "Client connected");

	// Claim the client fd.
	int epoll_fd = setup_epoll(ics);
	if (epoll_fd < 0) {
		return;
	}

	uint8_t buf[IPC_BUF_SIZE] = {0};

	while (ics->server->running) {
		const int half_a_second_ms = 500;
		struct epoll_event event = XRT_STRUCT_INIT;

		// We use epoll here to be able to timeout.
		int ret = epoll_wait(epoll_fd, &event, 1, half_a_second_ms);
		if (ret < 0) {
			IPC_ERROR(ics->server, "Failed epoll_wait '%i', disconnecting client.", ret);
			break;
		}

		// Timed out, loop again.
		if (ret == 0) {
			continue;
		}

		// Detect clients disconnecting gracefully.
		if (ret > 0 && (event.events & EPOLLHUP) != 0) {
			IPC_INFO(ics->server, "Client disconnected.");
			break;
		}

		// Finally get the data that is waiting for us.
		//! @todo replace this call
		ssize_t len = recv(ics->imc.ipc_handle, &buf, IPC_BUF_SIZE, 0);
		if (len < 4) {
			IPC_ERROR(ics->server, "Invalid packet received, disconnecting client.");
			break;
		}

		// Check the first 4 bytes of the message and dispatch.
		ipc_command_t *ipc_command = (ipc_command_t *)buf;

		IPC_TRACE_BEGIN(ipc_dispatch);
		xrt_result_t result = ipc_dispatch(ics, ipc_command);
		IPC_TRACE_END(ipc_dispatch);

		if (result != XRT_SUCCESS) {
			IPC_ERROR(ics->server, "During packet handling, disconnecting client.");
			break;
		}
	}

	close(epoll_fd);
	epoll_fd = -1;

	// Multiple threads might be looking at these fields.
	os_mutex_lock(&ics->server->global_state.lock);

	ipc_message_channel_close((struct ipc_message_channel *)&ics->imc);

	ics->server->threads[ics->server_thread_index].state = IPC_THREAD_STOPPING;
	ics->server_thread_index = -1;
	memset((void *)&ics->client_state, 0, sizeof(struct ipc_app_state));

	os_mutex_unlock(&ics->server->global_state.lock);

	ipc_server_client_destroy_compositor(ics);

	// Make sure undestroyed spaces are unreferenced
	for (uint32_t i = 0; i < IPC_MAX_CLIENT_SPACES; i++) {
		// Cast away volatile.
		xrt_space_reference((struct xrt_space **)&ics->xspcs[i], NULL);
	}

	// Should we stop the server when a client disconnects?
	if (ics->server->exit_on_disconnect) {
		ics->server->running = false;
	}

	ipc_server_deactivate_session(ics);
}

#else // XRT_OS_WINDOWS

static void
client_loop(volatile struct ipc_client_state *ics)
{
	U_TRACE_SET_THREAD_NAME("IPC Client");

	IPC_INFO(ics->server, "Client connected");

	uint8_t buf[IPC_BUF_SIZE];

	while (ics->server->running) {
		DWORD len;
		if (!ReadFile(ics->imc.ipc_handle, buf, sizeof(buf), &len, NULL)) {
			DWORD err = GetLastError();
			if (err == ERROR_BROKEN_PIPE) {
				IPC_INFO(ics->server, "ReadFile from pipe: %d %s", err, ipc_winerror(err));
			} else {
				IPC_ERROR(ics->server, "ReadFile from pipe failed: %d %s", err, ipc_winerror(err));
			}
			break;
		}
		if (len < sizeof(ipc_command_t)) {
			IPC_ERROR(ics->server, "Invalid packet received, disconnecting client.");
			break;
		} else {
			// Check the first 4 bytes of the message and dispatch.
			ipc_command_t *ipc_command = (ipc_command_t *)buf;

			IPC_TRACE_BEGIN(ipc_dispatch);
			xrt_result_t result = ipc_dispatch(ics, ipc_command);
			IPC_TRACE_END(ipc_dispatch);

			if (result != XRT_SUCCESS) {
				IPC_ERROR(ics->server, "During packet handling, disconnecting client.");
				break;
			}
		}
	}

	// Multiple threads might be looking at these fields.
	os_mutex_lock(&ics->server->global_state.lock);

	ipc_message_channel_close((struct ipc_message_channel *)&ics->imc);

	ics->server->threads[ics->server_thread_index].state = IPC_THREAD_STOPPING;
	ics->server_thread_index = -1;
	memset((void *)&ics->client_state, 0, sizeof(struct ipc_app_state));

	os_mutex_unlock(&ics->server->global_state.lock);

	ipc_server_client_destroy_compositor(ics);

	// Make sure undestroyed spaces are unreferenced
	for (uint32_t i = 0; i < IPC_MAX_CLIENT_SPACES; i++) {
		// Cast away volatile.
		xrt_space_reference((struct xrt_space **)&ics->xspcs[i], NULL);
	}

	// Should we stop the server when a client disconnects?
	if (ics->server->exit_on_disconnect) {
		ics->server->running = false;
	}

	ipc_server_deactivate_session(ics);
}

#endif // XRT_OS_WINDOWS

/*
 *
 * 'Exported' functions.
 *
 */

void
ipc_server_client_destroy_compositor(volatile struct ipc_client_state *ics)
{
	// Multiple threads might be looking at these fields.
	os_mutex_lock(&ics->server->global_state.lock);

	ics->swapchain_count = 0;

	// Destroy all swapchains now.
	for (uint32_t j = 0; j < IPC_MAX_CLIENT_SWAPCHAINS; j++) {
		// Drop our reference, does NULL checking. Cast away volatile.
		xrt_swapchain_reference((struct xrt_swapchain **)&ics->xscs[j], NULL);
		ics->swapchain_data[j].active = false;
		IPC_TRACE(ics->server, "Destroyed swapchain %d.", j);
	}

	for (uint32_t j = 0; j < IPC_MAX_CLIENT_SEMAPHORES; j++) {
		// Drop our reference, does NULL checking. Cast away volatile.
		xrt_compositor_semaphore_reference((struct xrt_compositor_semaphore **)&ics->xcsems[j], NULL);
		IPC_TRACE(ics->server, "Destroyed compositor semaphore %d.", j);
	}

	os_mutex_unlock(&ics->server->global_state.lock);

	// Cast away volatile.
	xrt_comp_destroy((struct xrt_compositor **)&ics->xc);
}

void *
ipc_server_client_thread(void *_ics)
{
	volatile struct ipc_client_state *ics = (volatile struct ipc_client_state *)_ics;

	client_loop(ics);

	return NULL;
}
