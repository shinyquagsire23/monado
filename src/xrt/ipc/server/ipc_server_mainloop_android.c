// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Server mainloop details on Android.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_server
 */

#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_os.h"

#include "os/os_time.h"
#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"

#include "server/ipc_server.h"
#include "server/ipc_server_mainloop_android.h"

#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define SHUTTING_DOWN (-1)

/*
 *
 * Static functions.
 *
 */

static int
init_pipe(struct ipc_server_mainloop *ml)
{
	int pipefd[2];
	int ret = pipe(pipefd);
	if (ret < 0) {
		U_LOG_E("pipe2() failed '%i'", ret);
		return ret;
	}
	ml->pipe_read = pipefd[0];
	ml->pipe_write = pipefd[1];
	return 0;
}

static int
init_epoll(struct ipc_server_mainloop *ml)
{
	int ret = epoll_create1(EPOLL_CLOEXEC);
	if (ret < 0) {
		return ret;
	}

	pthread_mutex_init(&ml->client_push_mutex, NULL);
	pthread_cond_init(&ml->accept_cond, NULL);
	pthread_mutex_init(&ml->accept_mutex, NULL);
	ml->epoll_fd = ret;

	struct epoll_event ev = {0};


	ev.events = EPOLLIN;
	ev.data.fd = ml->pipe_read;
	ret = epoll_ctl(ml->epoll_fd, EPOLL_CTL_ADD, ml->pipe_read, &ev);
	if (ret < 0) {
		U_LOG_E("epoll_ctl(pipe_read) failed '%i'", ret);
		return ret;
	}

	return 0;
}


static void
handle_listen(struct ipc_server *vs, struct ipc_server_mainloop *ml)
{
	int newfd = 0;
	pthread_mutex_lock(&ml->accept_mutex);
	if (read(ml->pipe_read, &newfd, sizeof(newfd)) == sizeof(newfd)) {
		// client_push_mutex should prevent dropping acknowledgements
		assert(ml->last_accepted_fd == 0);
		// Release the thread that gave us this fd.
		ml->last_accepted_fd = newfd;
		ipc_server_start_client_listener_thread(vs, newfd);
		pthread_cond_broadcast(&ml->accept_cond);
	} else {
		U_LOG_E("error on pipe read");
		ipc_server_handle_failure(vs);
		return;
	}
	pthread_mutex_unlock(&ml->accept_mutex);
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
		// Somebody new at the door.
		if (events[i].data.fd == ml->pipe_read) {
			handle_listen(vs, ml);
		}
	}
}

int
ipc_server_mainloop_init(struct ipc_server_mainloop *ml)
{
	int ret = init_pipe(ml);
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
	if (ml == NULL) {
		return;
	}
	if (ml->pipe_read > 0) {
		// Close pipe on exit
		close(ml->pipe_read);
		ml->pipe_read = -1;
	}
	//! @todo close pipe_write or epoll_fd?

	// Tell everybody we're done and they should go away.
	pthread_mutex_lock(&ml->accept_mutex);
	while (ml->last_accepted_fd != 0) {
		// Don't accidentally intervene in somebody else's message,
		// wait until there's no unblocks pending.
		pthread_cond_wait(&ml->accept_cond, &ml->accept_mutex);
	}
	ml->last_accepted_fd = SHUTTING_DOWN;
	pthread_cond_broadcast(&ml->accept_cond);
	pthread_mutex_unlock(&ml->accept_mutex);
}

int
ipc_server_mainloop_add_fd(struct ipc_server *vs, struct ipc_server_mainloop *ml, int newfd)
{
	// Take the client push lock here, serializing clients attempting to connect.
	// This one won't be unlocked when waiting on the condition variable, ensuring we keep other clients out.
	pthread_mutex_lock(&ml->client_push_mutex);

	// Take the lock here, so we don't accidentally miss our fd being accepted.
	pthread_mutex_lock(&ml->accept_mutex);

	// Write our fd number: the other side of the pipe is in the same process, so passing just the number is OK.
	int ret = write(ml->pipe_write, &newfd, sizeof(newfd));
	if (ret < 0) {
		U_LOG_E("write to pipe failed with '%i'.", ret);
		goto exit;
	}

	// Normal looping on the condition variable's condition.
	while (ml->last_accepted_fd != newfd && ml->last_accepted_fd != SHUTTING_DOWN) {
		ret = pthread_cond_wait(&ml->accept_cond, &ml->accept_mutex);
		if (ret < 0) {
			U_LOG_E("pthread_cond_wait failed with '%i'.", ret);
			goto exit;
		}
	}
	if (ml->last_accepted_fd == SHUTTING_DOWN) {
		// we actually didn't hand off our client, we should error out.
		U_LOG_W("server was shutting down.");
		ret = -1;
	} else {
		// OK, we have now been accepted. Zero out the last accepted fd.
		ml->last_accepted_fd = 0;
		ret = 0;
	}
exit:
	pthread_mutex_unlock(&ml->accept_mutex);
	pthread_mutex_unlock(&ml->client_push_mutex);
	return ret;
}
