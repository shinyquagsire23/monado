// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Per client thread listening on the socket.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_server
 */

#include "xrt/xrt_gfx_native.h"

#include "util/u_misc.h"

#include "server/ipc_server.h"
#include "ipc_server_generated.h"

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
	int listen_socket = ics->imc.socket_fd;
	assert(listen_socket >= 0);

	int ret = epoll_create1(EPOLL_CLOEXEC);
	if (ret < 0) {
		return ret;
	}

	int epoll_fd = ret;

	struct epoll_event ev = {0};

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
	IPC_INFO(ics->server, "Client connected");

	// Make sure it's ready for the client.
	u_rt_helper_client_clear((struct u_rt_helper *)&ics->urth);

	// Claim the client fd.
	int epoll_fd = setup_epoll(ics);
	if (epoll_fd < 0) {
		return;
	}

	uint8_t buf[IPC_BUF_SIZE];

	while (ics->server->running) {
		const int half_a_second_ms = 500;
		struct epoll_event event = {0};

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
		ssize_t len = recv(ics->imc.socket_fd, &buf, IPC_BUF_SIZE, 0);
		if (len < 4) {
			IPC_ERROR(ics->server, "Invalid packet received, disconnecting client.");
			break;
		}

		// Check the first 4 bytes of the message and dispatch.
		ipc_command_t *ipc_command = (uint32_t *)buf;
		xrt_result_t result = ipc_dispatch(ics, ipc_command);
		if (result != XRT_SUCCESS) {
			IPC_ERROR(ics->server, "During packet handling, disconnecting client.");
			break;
		}
	}

	close(epoll_fd);
	epoll_fd = -1;

	// Multiple threads might be looking at these fields.
	os_mutex_lock(&ics->server->global_state_lock);

	ipc_message_channel_close((struct ipc_message_channel *)&ics->imc);

	// Reset the urth for the next client.
	u_rt_helper_client_clear((struct u_rt_helper *)&ics->urth);

	ics->num_swapchains = 0;

	ics->server->threads[ics->server_thread_index].state = IPC_THREAD_STOPPING;
	ics->server_thread_index = -1;
	memset((void *)&ics->client_state, 0, sizeof(struct ipc_app_state));

	// Make sure to reset the renderstate fully.
	ics->rendering_state = false;
	ics->render_state.num_layers = 0;
	for (uint32_t i = 0; i < ARRAY_SIZE(ics->render_state.layers); ++i) {
		volatile struct ipc_layer_entry *rl = &ics->render_state.layers[i];

		rl->swapchain_ids[0] = 0;
		rl->swapchain_ids[1] = 0;
		rl->data.flip_y = false;
		/*!
		 * @todo this is redundant, we're setting both elements of a
		 * union. Why? Can we just zero the whole render_state?
		 */
		rl->data.stereo.l.sub.image_index = 0;
		rl->data.stereo.r.sub.image_index = 0;
		rl->data.quad.sub.image_index = 0;
		rl->data.cube.sub.image_index = 0;
		rl->data.cylinder.sub.image_index = 0;
		rl->data.equirect1.sub.image_index = 0;
		rl->data.equirect2.sub.image_index = 0;

		//! @todo set rects or array index?
	}

	// Destroy all swapchains now.
	for (uint32_t j = 0; j < IPC_MAX_CLIENT_SWAPCHAINS; j++) {
		xrt_swapchain_destroy((struct xrt_swapchain **)&ics->xscs[j]);
		ics->swapchain_data[j].active = false;
		IPC_TRACE(ics->server, "Destroyed swapchain %d.", j);
	}

	os_mutex_unlock(&ics->server->global_state_lock);

	// Should we stop the server when a client disconnects?
	if (ics->server->exit_on_disconnect) {
		ics->server->running = false;
	}
}


/*
 *
 * Entry point.
 *
 */

void *
ipc_server_client_thread(void *_ics)
{
	volatile struct ipc_client_state *ics = _ics;

	client_loop(ics);

	update_server_state(ics->server);

	return NULL;
}
