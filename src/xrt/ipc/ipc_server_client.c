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

ipc_result_t
ipc_handle_instance_get_shm_fd(volatile struct ipc_client_state *cs,
                               size_t max_num_fds,
                               int *out_fds,
                               size_t *out_num_fds)
{
	assert(max_num_fds >= 1);

	out_fds[0] = cs->server->ism_fd;
	*out_num_fds = 1;
	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_session_begin(volatile struct ipc_client_state *cs)
{
	cs->active = true;
	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_session_end(volatile struct ipc_client_state *cs)
{
	cs->active = false;
	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_compositor_get_formats(volatile struct ipc_client_state *cs,
                                  struct ipc_formats_info *out_info)
{
	out_info->num_formats = cs->xc->num_formats;
	for (size_t i = 0; i < cs->xc->num_formats; i++) {
		out_info->formats[i] = cs->xc->formats[i];
	}

	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_compositor_wait_frame(volatile struct ipc_client_state *cs)
{
	ipc_server_wait_add_frame(cs->server->iw, cs);
	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_compositor_begin_frame(volatile struct ipc_client_state *cs)
{
	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_compositor_discard_frame(volatile struct ipc_client_state *cs)
{
	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_compositor_layer_sync(volatile struct ipc_client_state *cs,
                                 uint32_t slot_id,
                                 uint32_t *out_free_slot_id)
{
	struct ipc_shared_memory *ism = cs->server->ism;
	struct ipc_layer_slot *slot = &ism->slots[slot_id];
	struct ipc_layer_entry *layer = &slot->layers[0];
	struct ipc_layer_stereo_projection *stereo = &layer->stereo;

	cs->render_state.flip_y = layer->flip_y;
	cs->render_state.l_swapchain_index = stereo->l.swapchain_id;
	cs->render_state.l_image_index = stereo->l.image_index;
	cs->render_state.r_swapchain_index = stereo->r.swapchain_id;
	cs->render_state.r_image_index = stereo->r.image_index;
	cs->render_state.rendering = true;

	*out_free_slot_id = (slot_id + 1) % IPC_MAX_SLOTS;

	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_swapchain_create(volatile struct ipc_client_state *cs,
                            uint32_t width,
                            uint32_t height,
                            uint64_t format,
                            uint32_t *out_id,
                            uint32_t *out_num_images,
                            uint64_t *out_size,
                            size_t max_num_fds,
                            int *out_fds,
                            size_t *out_num_fds)
{

	enum xrt_swapchain_create_flags flags =
	    XRT_SWAPCHAIN_CREATE_STATIC_IMAGE;
	enum xrt_swapchain_usage_bits usage =
	    XRT_SWAPCHAIN_CREATE_STATIC_IMAGE | XRT_SWAPCHAIN_USAGE_COLOR;
	uint32_t sample_count = 1;
	uint32_t face_count = 1;
	uint32_t array_size = 1;
	uint32_t mip_count = 1;

	// create the swapchain
	struct xrt_swapchain *xsc =
	    xrt_comp_create_swapchain(cs->xc,       // Compositor
	                              flags,        // Flags
	                              usage,        // Usage
	                              format,       // Format
	                              sample_count, // Sample count
	                              width,        // Width
	                              height,       // Height
	                              face_count,   // Face count
	                              array_size,   // Array size
	                              mip_count);   // Mip count

	uint32_t index = cs->num_swapchains;
	uint32_t num_images = xsc->num_images;
	// Our handle is just the index for now
	uint32_t handle = index;

	cs->xscs[index] = xsc;
	cs->swapchain_data[index].width = width;
	cs->swapchain_data[index].height = height;
	cs->swapchain_data[index].format = format;
	cs->swapchain_data[index].num_images = num_images;
	cs->swapchain_handles[index] = handle;

	// It's now safe to increment the number of swapchains.
	cs->num_swapchains++;

	// return our result to the caller.
	struct xrt_swapchain_fd *xcsfd = (struct xrt_swapchain_fd *)xsc;

	// Sanity checking.
	assert(num_images <= IPC_MAX_SWAPCHAIN_FDS);
	assert(num_images <= max_num_fds);

	*out_id = handle;
	*out_size = xcsfd->images[0].size;
	*out_num_images = num_images;

	// Setup the fds.
	*out_num_fds = num_images;
	for (size_t i = 0; i < num_images; i++) {
		out_fds[i] = xcsfd->images[i].fd;
	}

	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_swapchain_wait_image(volatile struct ipc_client_state *cs,
                                uint32_t id,
                                uint64_t timeout,
                                uint32_t index)
{
	//! @todo Look up the index.
	uint32_t sc_index = id;
	struct xrt_swapchain *xsc = cs->xscs[sc_index];

	xrt_swapchain_wait_image(xsc, timeout, index);

	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_swapchain_acquire_image(volatile struct ipc_client_state *cs,
                                   uint32_t id,
                                   uint32_t *out_index)

{
	//! @todo Look up the index.
	uint32_t sc_index = id;
	struct xrt_swapchain *xsc = cs->xscs[sc_index];

	xrt_swapchain_acquire_image(xsc, out_index);

	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_swapchain_release_image(volatile struct ipc_client_state *cs,
                                   uint32_t id,
                                   uint32_t index)
{
	//! @todo Look up the index.
	uint32_t sc_index = id;
	struct xrt_swapchain *xsc = cs->xscs[sc_index];

	xrt_swapchain_release_image(xsc, index);

	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_swapchain_destroy(volatile struct ipc_client_state *cs, uint32_t id)
{
	//! @todo Implement destroy swapchain.
	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_device_update_input(volatile struct ipc_client_state *cs,
                               uint32_t id)
{
	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct ipc_shared_memory *ism = cs->server->ism;
	struct xrt_device *xdev = cs->server->xdevs[device_id];
	struct ipc_shared_device *idev = &ism->idevs[device_id];

	// Update inputs.
	xrt_device_update_inputs(xdev);

	// Copy data into the shared memory.
	struct xrt_input *src = xdev->inputs;
	struct xrt_input *dst = &ism->inputs[idev->first_input_index];
	memcpy(dst, src, sizeof(struct xrt_input) * idev->num_inputs);

	// Reply.
	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_device_get_tracked_pose(volatile struct ipc_client_state *cs,
                                   uint32_t id,
                                   enum xrt_input_name name,
                                   uint64_t at_timestamp,
                                   uint64_t *out_timestamp,
                                   struct xrt_space_relation *out_relation)
{

	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct xrt_device *xdev = cs->server->xdevs[device_id];

	// Get the pose.
	xrt_device_get_tracked_pose(xdev, name, at_timestamp, out_timestamp,
	                            out_relation);

	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_device_get_view_pose(volatile struct ipc_client_state *cs,
                                uint32_t id,
                                struct xrt_vec3 *eye_relation,
                                uint32_t view_index,
                                struct xrt_pose *out_pose)
{

	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct xrt_device *xdev = cs->server->xdevs[device_id];

	// Get the pose.
	xrt_device_get_view_pose(xdev, eye_relation, view_index, out_pose);

	return IPC_SUCCESS;
}

ipc_result_t
ipc_handle_device_set_output(volatile struct ipc_client_state *cs,
                             uint32_t id,
                             enum xrt_output_name name,
                             union xrt_output_value *value)
{
	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct xrt_device *xdev = cs->server->xdevs[device_id];

	// Set the output.
	xrt_device_set_output(xdev, name, value);

	return IPC_SUCCESS;
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
client_loop(volatile struct ipc_client_state *cs)
{
	fprintf(stderr, "SERVER: Client connected\n");

	// Claim the client fd.
	int epoll_fd = setup_epoll(cs->ipc_socket_fd);
	if (epoll_fd < 0) {
		return;
	}

	uint8_t buf[IPC_BUF_SIZE];

	while (cs->server->running) {
		const int half_a_second_ms = 500;
		struct epoll_event event = {0};

		// We use epoll here to be able to timeout.
		int ret = epoll_wait(epoll_fd, &event, 1, half_a_second_ms);
		if (ret < 0) {
			fprintf(stderr,
			        "ERROR: Failed epoll_wait '%i', disconnecting "
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
		ssize_t len = recv(cs->ipc_socket_fd, &buf, IPC_BUF_SIZE, 0);
		if (len < 4) {
			fprintf(stderr,
			        "ERROR: Invalid packet received, disconnecting "
			        "client.\n");
			break;
		}

		// Check the first 4 bytes of the message and dispatch.
		ipc_command_t *ipc_command = (uint32_t *)buf;
		ret = ipc_dispatch(cs, ipc_command);
		if (ret < 0) {
			fprintf(stderr,
			        "ERROR: During packet handling, disconnecting "
			        "client.\n");
			break;
		}
	}

	close(epoll_fd);
	epoll_fd = -1;

	close(cs->ipc_socket_fd);
	cs->ipc_socket_fd = -1;

	cs->active = false;
	cs->num_swapchains = 0;

	// Make sure to reset the renderstate fully.
	cs->render_state.flip_y = false;
	cs->render_state.l_swapchain_index = 0;
	cs->render_state.l_image_index = 0;
	cs->render_state.r_swapchain_index = 0;
	cs->render_state.r_image_index = 0;
	cs->render_state.rendering = false;

	for (uint32_t j = 0; j < IPC_MAX_CLIENT_SWAPCHAINS; j++) {
		xrt_swapchain_destroy((struct xrt_swapchain **)&cs->xscs[j]);
		cs->swapchain_handles[j] = -1;
	}

	// Should we stop the server when a client disconnects?
	if (cs->server->exit_on_disconnect) {
		cs->server->running = false;
	}
}


/*
 *
 * Entry point.
 *
 */

void *
ipc_server_client_thread(void *_cs)
{
	volatile struct ipc_client_state *cs = _cs;

	client_loop(cs);

	cs->server->thread_stopping = true;
	cs->server->thread_started = false;

	return NULL;
}
