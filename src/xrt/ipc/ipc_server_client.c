// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common server side code.
 * @author Pete Black <pblack@collabora.com>
 * @ingroup ipc_server
 */

#include "xrt/xrt_gfx_fd.h"

#include "util/u_misc.h"

#include "ipc_server.h"
#include "ipc_server_utils.h"
#include "ipc_server_generated.h"

#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <sys/epoll.h>


/*
 *
 * Handle functions.
 *
 */

xrt_result_t
ipc_handle_instance_get_shm_fd(volatile struct ipc_client_state *cs,
                               size_t max_num_fds,
                               int *out_fds,
                               size_t *out_num_fds)
{
	assert(max_num_fds >= 1);

	out_fds[0] = cs->server->ism_fd;
	*out_num_fds = 1;
	return XRT_SUCCESS;
}
xrt_result_t
ipc_handle_session_create(volatile struct ipc_client_state *ics,
                          struct xrt_session_prepare_info *xspi)
{
	ics->client_state.session_active = false;
	ics->client_state.session_overlay = false;
	ics->client_state.session_visible = false;

	if (xspi->is_overlay) {
		ics->client_state.session_overlay = true;
		ics->client_state.z_order = xspi->z_order;
	}

	update_server_state(ics->server);
	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_session_begin(volatile struct ipc_client_state *ics)
{
	// ics->client_state.session_active = true;
	// update_server_state(ics->server);
	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_session_end(volatile struct ipc_client_state *ics)
{
	ics->client_state.session_active = false;
	update_server_state(ics->server);
	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_get_formats(volatile struct ipc_client_state *ics,
                                  struct ipc_formats_info *out_info)
{
	out_info->num_formats = ics->xc->num_formats;
	for (size_t i = 0; i < ics->xc->num_formats; i++) {
		out_info->formats[i] = ics->xc->formats[i];
	}

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_wait_frame(volatile struct ipc_client_state *ics,
                                 int64_t *out_frame_id,
                                 uint64_t *predicted_display_time,
                                 uint64_t *wake_up_time,
                                 uint64_t *predicted_display_period,
                                 uint64_t *min_display_period)
{
	os_mutex_lock(&ics->server->global_state_lock);

	u_rt_helper_predict((struct u_rt_helper *)&ics->urth, out_frame_id,
	                    predicted_display_time, wake_up_time,
	                    predicted_display_period, min_display_period);

	os_mutex_unlock(&ics->server->global_state_lock);

	ics->client_state.session_active = true;
	update_server_state(ics->server);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_wait_woke(volatile struct ipc_client_state *ics,
                                int64_t frame_id)
{
	os_mutex_lock(&ics->server->global_state_lock);

	u_rt_helper_mark_wait_woke((struct u_rt_helper *)&ics->urth, frame_id);

	os_mutex_unlock(&ics->server->global_state_lock);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_begin_frame(volatile struct ipc_client_state *ics,
                                  int64_t frame_id)
{
	os_mutex_lock(&ics->server->global_state_lock);

	u_rt_helper_mark_begin((struct u_rt_helper *)&ics->urth, frame_id);

	os_mutex_unlock(&ics->server->global_state_lock);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_discard_frame(volatile struct ipc_client_state *ics,
                                    int64_t frame_id)
{
	os_mutex_lock(&ics->server->global_state_lock);

	u_rt_helper_mark_discarded((struct u_rt_helper *)&ics->urth, frame_id);

	os_mutex_unlock(&ics->server->global_state_lock);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_layer_sync(volatile struct ipc_client_state *ics,
                                 int64_t frame_id,
                                 uint32_t slot_id,
                                 uint32_t *out_free_slot_id)
{
	struct ipc_shared_memory *ism = ics->server->ism;
	struct ipc_layer_slot *slot = &ism->slots[slot_id];

	// Copy current slot data to our state.
	ics->render_state = *slot;
	ics->rendering_state = true;

	os_mutex_lock(&ics->server->global_state_lock);

	*out_free_slot_id =
	    (ics->server->current_slot_index + 1) % IPC_MAX_SLOTS;
	ics->server->current_slot_index = *out_free_slot_id;

	// Also protected by the global lock.
	u_rt_helper_mark_delivered((struct u_rt_helper *)&ics->urth, frame_id);

	os_mutex_unlock(&ics->server->global_state_lock);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_poll_events(volatile struct ipc_client_state *cs,
                                  union xrt_compositor_event *out_xce)
{
	uint64_t l_timestamp = UINT64_MAX;
	volatile struct ipc_queued_event *event_to_send = NULL;
	for (uint32_t i = 0; i < IPC_EVENT_QUEUE_SIZE; i++) {
		volatile struct ipc_queued_event *e = &cs->queued_events[i];
		if (e->pending == true && e->timestamp < l_timestamp) {
			event_to_send = e;
		}
	}

	// We always return an event in response to this call -
	// We signal no events with a special event type.
	out_xce->type = XRT_COMPOSITOR_EVENT_NONE;

	if (event_to_send) {
		*out_xce = event_to_send->event;
		event_to_send->pending = false;
	}

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_get_client_info(volatile struct ipc_client_state *_ics,
                                  uint32_t id,
                                  struct ipc_app_state *out_client_desc)
{
	if (id >= IPC_MAX_CLIENTS) {
		return XRT_ERROR_IPC_FAILURE;
	}
	volatile struct ipc_client_state *ics = &_ics->server->threads[id].ics;

	if (ics->ipc_socket_fd <= 0) {
		return XRT_ERROR_IPC_FAILURE;
	}

	*out_client_desc = ics->client_state;

	//@todo: track this data in the ipc_client_state struct
	out_client_desc->primary_application = false;
	if (ics->server->active_client_index == (int)id) {
		out_client_desc->primary_application = true;
	}

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_set_client_info(volatile struct ipc_client_state *ics,
                                  struct ipc_app_state *client_desc)
{
	ics->client_state.info = client_desc->info;
	ics->client_state.pid = client_desc->pid;
	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_get_clients(volatile struct ipc_client_state *_ics,
                              struct ipc_client_list *list)
{
	for (uint32_t i = 0; i < IPC_MAX_CLIENTS; i++) {
		list->ids[i] = _ics->server->threads[i].ics.server_thread_index;
	}
	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_set_primary_client(volatile struct ipc_client_state *ics,
                                     uint32_t client_id)
{

	ics->server->active_client_index = client_id;
	printf("system setting active client to %d\n", client_id);
	update_server_state(ics->server);
	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_set_focused_client(volatile struct ipc_client_state *ics,
                                     uint32_t client_id)
{
	printf("UNIMPLEMENTED: system setting focused client to %d\n",
	       client_id);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_swapchain_create(volatile struct ipc_client_state *ics,
                            struct xrt_swapchain_create_info *info,
                            uint32_t *out_id,
                            uint32_t *out_num_images,
                            uint64_t *out_size,
                            size_t max_num_fds,
                            int *out_fds,
                            size_t *out_num_fds)
{
	// Our handle is just the index for now.
	uint32_t index = 0;
	for (; index < IPC_MAX_CLIENT_SWAPCHAINS; index++) {
		if (!ics->swapchain_data[index].active) {
			break;
		}
	}

	if (index >= IPC_MAX_CLIENT_SWAPCHAINS) {
		fprintf(stderr, "ERROR: Too many swapchains!\n");
		return XRT_ERROR_IPC_FAILURE;
	}

	// It's now safe to increment the number of swapchains.
	ics->num_swapchains++;

	// create the swapchain
	struct xrt_swapchain *xsc = xrt_comp_create_swapchain(ics->xc, info);

	uint32_t num_images = xsc->num_images;

	IPC_SPEW(ics->server, "IPC: Created swapchain %d\n", index);

	ics->xscs[index] = xsc;
	ics->swapchain_data[index].active = true;
	ics->swapchain_data[index].width = info->width;
	ics->swapchain_data[index].height = info->height;
	ics->swapchain_data[index].format = info->format;
	ics->swapchain_data[index].num_images = num_images;

	// return our result to the caller.
	struct xrt_swapchain_fd *xcsfd = (struct xrt_swapchain_fd *)xsc;

	// Sanity checking.
	assert(num_images <= IPC_MAX_SWAPCHAIN_FDS);
	assert(num_images <= max_num_fds);

	*out_id = index;
	*out_size = xcsfd->images[0].size;
	*out_num_images = num_images;

	// Setup the fds.
	*out_num_fds = num_images;
	for (size_t i = 0; i < num_images; i++) {
		out_fds[i] = xcsfd->images[i].fd;
	}

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_swapchain_wait_image(volatile struct ipc_client_state *ics,
                                uint32_t id,
                                uint64_t timeout,
                                uint32_t index)
{
	//! @todo Look up the index.
	uint32_t sc_index = id;
	struct xrt_swapchain *xsc = ics->xscs[sc_index];

	xrt_swapchain_wait_image(xsc, timeout, index);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_swapchain_acquire_image(volatile struct ipc_client_state *ics,
                                   uint32_t id,
                                   uint32_t *out_index)

{
	//! @todo Look up the index.
	uint32_t sc_index = id;
	struct xrt_swapchain *xsc = ics->xscs[sc_index];

	xrt_swapchain_acquire_image(xsc, out_index);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_swapchain_release_image(volatile struct ipc_client_state *ics,
                                   uint32_t id,
                                   uint32_t index)
{
	//! @todo Look up the index.
	uint32_t sc_index = id;
	struct xrt_swapchain *xsc = ics->xscs[sc_index];

	xrt_swapchain_release_image(xsc, index);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_swapchain_destroy(volatile struct ipc_client_state *ics, uint32_t id)
{
	//! @todo Implement destroy swapchain.
	ics->num_swapchains--;

	xrt_swapchain_destroy((struct xrt_swapchain **)&ics->xscs[id]);
	ics->swapchain_data[id].active = false;

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_device_update_input(volatile struct ipc_client_state *ics,
                               uint32_t id)
{
	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct ipc_shared_memory *ism = ics->server->ism;
	struct xrt_device *xdev = ics->server->xdevs[device_id];
	struct ipc_shared_device *idev = &ism->idevs[device_id];

	// Update inputs.
	xrt_device_update_inputs(xdev);

	// Copy data into the shared memory.
	struct xrt_input *src = xdev->inputs;
	struct xrt_input *dst = &ism->inputs[idev->first_input_index];
	memcpy(dst, src, sizeof(struct xrt_input) * idev->num_inputs);

	// Reply.
	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_device_get_tracked_pose(volatile struct ipc_client_state *ics,
                                   uint32_t id,
                                   enum xrt_input_name name,
                                   uint64_t at_timestamp,
                                   uint64_t *out_timestamp,
                                   struct xrt_space_relation *out_relation)
{

	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct xrt_device *xdev = ics->server->xdevs[device_id];

	// Get the pose.
	xrt_device_get_tracked_pose(xdev, name, at_timestamp, out_timestamp,
	                            out_relation);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_device_get_view_pose(volatile struct ipc_client_state *ics,
                                uint32_t id,
                                struct xrt_vec3 *eye_relation,
                                uint32_t view_index,
                                struct xrt_pose *out_pose)
{

	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct xrt_device *xdev = ics->server->xdevs[device_id];

	// Get the pose.
	xrt_device_get_view_pose(xdev, eye_relation, view_index, out_pose);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_device_set_output(volatile struct ipc_client_state *ics,
                             uint32_t id,
                             enum xrt_output_name name,
                             union xrt_output_value *value)
{
	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct xrt_device *xdev = ics->server->xdevs[device_id];

	// Set the output.
	xrt_device_set_output(xdev, name, value);

	return XRT_SUCCESS;
}


/*
 *
 * Helper functions.
 *
 */

static int
setup_epoll(int listen_socket)
{
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
		fprintf(stderr, "ERROR: epoll_ctl(listen_socket) failed '%i'\n",
		        ret);
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
	fprintf(stderr, "SERVER: Client connected\n");

	// Make sure it's ready for the client.
	u_rt_helper_client_clear((struct u_rt_helper *)&ics->urth);

	// Claim the client fd.
	int epoll_fd = setup_epoll(ics->ipc_socket_fd);
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
			fprintf(stderr,
			        "ERROR: Failed epoll_wait '%i', "
			        "disconnecting "
			        "client.\n",
			        ret);
			break;
		}

		// Timed out, loop again.
		if (ret == 0) {
			continue;
		}

		// Detect clients disconnecting gracefully.
		if (ret > 0 && (event.events & EPOLLHUP) != 0) {
			fprintf(stderr, "SERVER: Client disconnected\n");
			break;
		}

		// Finally get the data that is waiting for us.
		ssize_t len = recv(ics->ipc_socket_fd, &buf, IPC_BUF_SIZE, 0);
		if (len < 4) {
			fprintf(stderr,
			        "ERROR: Invalid packet received, "
			        "disconnecting "
			        "client.\n");
			break;
		}

		// Check the first 4 bytes of the message and dispatch.
		ipc_command_t *ipc_command = (uint32_t *)buf;
		xrt_result_t result = ipc_dispatch(ics, ipc_command);
		if (result != XRT_SUCCESS) {
			fprintf(stderr,
			        "ERROR: During packet handling, "
			        "disconnecting "
			        "client.\n");
			break;
		}
	}

	close(epoll_fd);
	epoll_fd = -1;

	// Multiple threads might be looking at these fields.
	os_mutex_lock(&ics->server->global_state_lock);

	close(ics->ipc_socket_fd);
	ics->ipc_socket_fd = -1;

	// Reset the urth for the next client.
	u_rt_helper_client_clear((struct u_rt_helper *)&ics->urth);

	ics->num_swapchains = 0;

	ics->server->threads[ics->server_thread_index].state =
	    IPC_THREAD_STOPPING;
	ics->server_thread_index = -1;
	memset((void *)&ics->client_state, 0, sizeof(struct ipc_app_state));

	// Make sure to reset the renderstate fully.
	ics->rendering_state = false;
	ics->render_state.num_layers = 0;
	for (uint32_t i = 0; i < ARRAY_SIZE(ics->render_state.layers); ++i) {
		volatile struct ipc_layer_entry *rl =
		    &ics->render_state.layers[i];

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

		//! @todo set rects or array index?
	}

	// Destroy all swapchains now.
	for (uint32_t j = 0; j < IPC_MAX_CLIENT_SWAPCHAINS; j++) {
		xrt_swapchain_destroy((struct xrt_swapchain **)&ics->xscs[j]);
		ics->swapchain_data[j].active = false;
		IPC_SPEW(ics->server, "IPC: Destroyed swapchain %d\n", j);
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
