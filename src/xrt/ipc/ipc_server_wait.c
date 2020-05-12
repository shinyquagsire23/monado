// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Threads for blocking and waiting on things.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_server
 */

#include "xrt/xrt_gfx_fd.h"

#include "os/os_threading.h"
#include "xrt/xrt_compositor.h"
#include "util/u_misc.h"

#include "ipc_server.h"
#include "ipc_protocol.h"


struct ipc_wait
{
	// Owning server.
	struct ipc_server *s;

	//! Thread and lock helper.
	struct os_thread_helper oth;

	// Number of waiters.
	uint32_t num_waiters;

	// Client states waiting on this waitframe.
	volatile struct ipc_client_state *cs[IPC_MAX_CLIENTS];
};

static void *
run(void *ptr)
{
	struct ipc_wait *iw = (struct ipc_wait *)ptr;

	os_thread_helper_lock(&iw->oth);

	while (os_thread_helper_is_running_locked(&iw->oth)) {
		// No waiters, wait for waiters.
		if (iw->num_waiters <= 0) {
			os_thread_helper_wait_locked(&iw->oth);
		}

		// Where we woken up to shut down?
		if (!os_thread_helper_is_running_locked(&iw->oth)) {
			break;
		}

		// Just in case.
		if (iw->num_waiters <= 0) {
			continue;
		}

		// Unlock the mutex when we have waiting to do.
		os_thread_helper_unlock(&iw->oth);

		// Do the waiting.
		uint64_t predicted_display_time, predicted_display_period;
		xrt_comp_wait_frame(iw->s->xc, &predicted_display_time,
		                    &predicted_display_period);

		// Lock for broadcast.
		os_thread_helper_lock(&iw->oth);

		for (size_t i = 0; i < IPC_MAX_CLIENTS; i++) {
			volatile struct ipc_client_state *cs = iw->cs[i];
			iw->cs[i] = NULL;

			if (cs == NULL) {
				continue;
			}

			volatile struct ipc_shared_memory *ism = iw->s->ism;

			ism->wait_frame.predicted_display_time =
			    predicted_display_time;
			ism->wait_frame.predicted_display_period =
			    predicted_display_period;

			// Wake the client up now.
			sem_post((sem_t *)&ism->wait_frame.sem);
		}

		iw->num_waiters = 0;
	}

	os_thread_helper_unlock(&iw->oth);

	return NULL;
}

void
ipc_server_wait_add_frame(struct ipc_wait *iw,
                          volatile struct ipc_client_state *cs)
{
	os_thread_helper_lock(&iw->oth);

	// Don't do anything if we have stopped.
	if (!os_thread_helper_is_running_locked(&iw->oth)) {
		os_thread_helper_unlock(&iw->oth);
		return;
	}

	// Register the client to the list of waiters.
	iw->cs[iw->num_waiters++] = cs;

	// Wake up the thread.
	os_thread_helper_signal_locked(&iw->oth);

	os_thread_helper_unlock(&iw->oth);
}

void
ipc_server_wait_free(struct ipc_wait **out_iw)
{
	struct ipc_wait *iw = *out_iw;

	// Already freed, nothing to do.
	if (iw == NULL) {
		return;
	}

	// Destroy also stops the thread should it be running.
	os_thread_helper_destroy(&iw->oth);

	*out_iw = NULL;
	free(iw);
}

int
ipc_server_wait_alloc(struct ipc_server *s, struct ipc_wait **out_iw)
{
	struct ipc_wait *iw = U_TYPED_CALLOC(struct ipc_wait);
	iw->s = s;

	int ret = os_thread_helper_init(&iw->oth);
	if (ret < 0) {
		free(iw);
		return ret;
	}

	ret = os_thread_helper_start(&iw->oth, run, iw);
	if (ret < 0) {
		ipc_server_wait_free(&iw);
		return ret;
	}

	*out_iw = iw;

	return 0;
}
