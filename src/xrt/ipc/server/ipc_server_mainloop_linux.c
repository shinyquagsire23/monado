// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Server mainloop details on Linux.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_server
 */

#include "xrt/xrt_device.h"
#include "xrt/xrt_instance.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_os.h"

#include "os/os_time.h"
#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_trace_marker.h"
#include "util/u_file.h"

#include "shared/ipc_shmem.h"
#include "server/ipc_server.h"

#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#ifdef XRT_HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif


/*
 *
 * Static functions.
 *
 */
static int
get_systemd_socket(struct ipc_server_mainloop *ml, int *out_fd)
{
#ifdef XRT_HAVE_SYSTEMD
	// We may have been launched with socket activation
	int num_fds = sd_listen_fds(0);
	if (num_fds > 1) {
		U_LOG_E("Too many file descriptors passed by systemd.");
		return -1;
	}
	if (num_fds == 1) {
		*out_fd = SD_LISTEN_FDS_START + 0;
		ml->launched_by_socket = true;
		U_LOG_D("Got existing socket from systemd.");
	}
#endif
	return 0;
}

static int
create_listen_socket(struct ipc_server_mainloop *ml, int *out_fd)
{
	// no fd provided
	struct sockaddr_un addr;
	int fd;
	int ret;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		U_LOG_E("Message Socket Create Error!");
		return fd;
	}


	char sock_file[PATH_MAX];

	int size = u_file_get_path_in_runtime_dir(IPC_MSG_SOCK_FILE, sock_file, PATH_MAX);
	if (size == -1) {
		U_LOG_E("Could not get socket file name");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));

	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, sock_file);

	ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));

#ifdef XRT_HAVE_LIBBSD
	// no other instance is running, or we would have never arrived here
	if (ret < 0 && errno == EADDRINUSE) {
		U_LOG_W("Removing stale socket file %s", sock_file);

		ret = unlink(sock_file);
		if (ret < 0) {
			U_LOG_E("Failed to remove stale socket file %s: %s", sock_file, strerror(errno));
			return ret;
		}
		ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	}
#endif

	if (ret < 0) {
		U_LOG_E("Could not bind socket to path %s: %s. Is the service running already?", sock_file,
		        strerror(errno));
#ifdef XRT_HAVE_SYSTEMD
		U_LOG_E("Or, is the systemd unit monado.socket or monado-dev.socket active?");
#endif
		if (errno == EADDRINUSE) {
			U_LOG_E("If monado-service is not running, delete %s before starting a new instance",
			        sock_file);
		}
		close(fd);
		return ret;
	}
	// Save for later
	ml->socket_filename = strdup(sock_file);

	ret = listen(fd, IPC_MAX_CLIENTS);
	if (ret < 0) {
		close(fd);
		return ret;
	}
	U_LOG_D("Created listening socket %s.", sock_file);
	*out_fd = fd;
	return 0;
}

static int
init_listen_socket(struct ipc_server_mainloop *ml)
{
	int fd = -1;
	int ret;
	ml->listen_socket = -1;

	ret = get_systemd_socket(ml, &fd);
	if (ret < 0) {
		return ret;
	}

	if (fd == -1) {
		ret = create_listen_socket(ml, &fd);
		if (ret < 0) {
			return ret;
		}
	}
	// All ok!
	ml->listen_socket = fd;
	U_LOG_D("Listening socket is fd %d", ml->listen_socket);

	return fd;
}

static int
init_epoll(struct ipc_server_mainloop *ml)
{
	int ret = epoll_create1(EPOLL_CLOEXEC);
	if (ret < 0) {
		return ret;
	}

	ml->epoll_fd = ret;

	struct epoll_event ev = {0};

	if (!ml->launched_by_socket) {
		// Can't do this when launched by systemd socket activation by
		// default.
		// This polls stdin.
		ev.events = EPOLLIN;
		ev.data.fd = 0; // stdin
		ret = epoll_ctl(ml->epoll_fd, EPOLL_CTL_ADD, 0, &ev);
		if (ret < 0) {
			U_LOG_E("epoll_ctl(stdin) failed '%i'", ret);
			return ret;
		}
	}

	ev.events = EPOLLIN;
	ev.data.fd = ml->listen_socket;
	ret = epoll_ctl(ml->epoll_fd, EPOLL_CTL_ADD, ml->listen_socket, &ev);
	if (ret < 0) {
		U_LOG_E("epoll_ctl(listen_socket) failed '%i'", ret);
		return ret;
	}

	return 0;
}

static void
handle_listen(struct ipc_server *vs, struct ipc_server_mainloop *ml)
{
	int ret = accept(ml->listen_socket, NULL, NULL);
	if (ret < 0) {
		U_LOG_E("accept '%i'", ret);
		ipc_server_handle_failure(vs);
		return;
	}
	ipc_server_start_client_listener_thread(vs, ret);
}

#define NUM_POLL_EVENTS 8
#define NO_SLEEP 0

/*
 *
 * Exported functions
 *
 */

void
ipc_server_mainloop_poll(struct ipc_server *vs, struct ipc_server_mainloop *ml)
{
	IPC_TRACE_MARKER();

	int epoll_fd = ml->epoll_fd;

	struct epoll_event events[NUM_POLL_EVENTS] = {0};

	// No sleeping, returns immediately.
	int ret = epoll_wait(epoll_fd, events, NUM_POLL_EVENTS, NO_SLEEP);
	if (ret < 0) {
		U_LOG_E("epoll_wait failed with '%i'.", ret);
		ipc_server_handle_failure(vs);
		return;
	}

	for (int i = 0; i < ret; i++) {
		// If we get data on stdin, stop.
		if (events[i].data.fd == 0) {
			ipc_server_handle_shutdown_signal(vs);
			return;
		}

		// Somebody new at the door.
		if (events[i].data.fd == ml->listen_socket) {
			handle_listen(vs, ml);
		}
	}
}

int
ipc_server_mainloop_init(struct ipc_server_mainloop *ml)
{
	IPC_TRACE_MARKER();

	int ret = init_listen_socket(ml);
	if (ret < 0) {
		ipc_server_mainloop_deinit(ml);
		return ret;
	}

	ret = init_epoll(ml);
	if (ret < 0) {
		ipc_server_mainloop_deinit(ml);
		return ret;
	}
	return 0;
}

void
ipc_server_mainloop_deinit(struct ipc_server_mainloop *ml)
{
	IPC_TRACE_MARKER();

	if (ml == NULL) {
		return;
	}
	if (ml->listen_socket > 0) {
		// Close socket on exit
		close(ml->listen_socket);
		ml->listen_socket = -1;
		if (!ml->launched_by_socket && ml->socket_filename) {
			// Unlink it too, but only if we bound it.
			unlink(ml->socket_filename);
			free(ml->socket_filename);
			ml->socket_filename = NULL;
		}
	}
	//! @todo close epoll_fd?
}
