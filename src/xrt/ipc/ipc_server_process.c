// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Server process functions.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_server
 */

#include "ipc_server.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_instance.h"
#include "xrt/xrt_compositor.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"

#include "ipc_server_utils.h"

#include "main/comp_compositor.h"
#include "main/comp_renderer.h"

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


/*
 *
 * Defines and helpers.
 *
 */

#define IPC_MAX_CLIENTS 8

DEBUG_GET_ONCE_BOOL_OPTION(exit_on_disconnect, "IPC_EXIT_ON_DISCONNECT", false)


/*
 *
 * Static functions.
 *
 */

static void
teardown_all(struct ipc_server *s)
{
	u_var_remove_root(s);

	xrt_comp_destroy(&s->xc);

	for (size_t i = 0; i < IPC_SERVER_NUM_XDEVS; i++) {
		xrt_device_destroy(&s->xdevs[i]);
	}

	xrt_instance_destroy(&s->xinst);

	if (s->listen_socket > 0) {
		close(s->listen_socket);
		s->listen_socket = -1;
	}
}

static int
init_shm(struct ipc_server *s)
{
	const size_t size = sizeof(struct ipc_shared_memory);

	int fd = shm_open("/monado_shm", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		return -1;
	}

	if (ftruncate(fd, size) < 0) {
		close(fd);
		return -1;
	}

	const int access = PROT_READ | PROT_WRITE;
	const int flags = MAP_SHARED;
	s->ism = mmap(NULL, size, access, flags, fd, 0);
	if (s->ism == NULL) {
		close(fd);
		return -1;
	}

	// we have a filehandle, we will pass this to
	// our client rather than access via filesystem
	shm_unlink("/monado_shm");

	s->ism_fd = fd;


	/*
	 *
	 * Setup the shared memory state.
	 *
	 */

	uint32_t input_index = 0;
	uint32_t output_index = 0;
	uint32_t count = 0;
	struct ipc_shared_memory *ism = s->ism;

	for (size_t i = 0; i < IPC_SERVER_NUM_XDEVS; i++) {
		struct xrt_device *xdev = s->xdevs[i];
		if (xdev == NULL) {
			continue;
		}

		struct ipc_shared_device *idev = &ism->idevs[count++];

		idev->name = xdev->name;
		memcpy(idev->str, xdev->str, sizeof(idev->str));

		// Is this a HMD?
		if (xdev->hmd != NULL) {
			ism->hmd.views[0].display.w_pixels =
			    xdev->hmd->views[0].display.w_pixels;
			ism->hmd.views[0].display.h_pixels =
			    xdev->hmd->views[0].display.h_pixels;
			ism->hmd.views[0].fov = xdev->hmd->views[0].fov;
			ism->hmd.views[1].display.w_pixels =
			    xdev->hmd->views[1].display.w_pixels;
			ism->hmd.views[1].display.h_pixels =
			    xdev->hmd->views[1].display.h_pixels;
			ism->hmd.views[1].fov = xdev->hmd->views[1].fov;
		}

		// Initial update.
		xrt_device_update_inputs(xdev);

		// Copy the initial state and also count the number in inputs.
		size_t input_start = input_index;
		for (size_t k = 0; k < xdev->num_inputs; k++) {
			ism->inputs[input_index++] = xdev->inputs[k];
		}

		// Setup the 'offsets' and number of inputs.
		if (input_start != input_index) {
			idev->num_inputs = input_index - input_start;
			idev->first_input_index = input_start;
		}

		// Copy the initial state and also count the number in outputs.
		size_t output_start = output_index;
		for (size_t k = 0; k < xdev->num_outputs; k++) {
			ism->outputs[output_index++] = xdev->outputs[k];
		}

		// Setup the 'offsets' and number of outputs.
		if (output_start != output_index) {
			idev->num_outputs = output_index - output_start;
			idev->first_output_index = output_start;
		}
	}

	// Finally tell the client how many devices we have.
	s->ism->num_idevs = count;

	return 0;
}

static int
init_listen_socket(struct ipc_server *s)
{
	struct sockaddr_un addr;
	int fd, ret;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		fprintf(stderr, "Message Socket Create Error!\n");
		return fd;
	}

	memset(&addr, 0, sizeof(addr));

	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, IPC_MSG_SOCK_FILE);
	unlink(IPC_MSG_SOCK_FILE);

	ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		close(fd);
		return ret;
	}

	ret = listen(fd, IPC_MAX_CLIENTS);
	if (ret < 0) {
		close(fd);
		return ret;
	}

	// All ok!
	s->listen_socket = fd;

	return fd;
}

static int
init_epoll(struct ipc_server *s)
{
	int ret = epoll_create1(EPOLL_CLOEXEC);
	if (ret < 0) {
		return ret;
	}

	s->epoll_fd = ret;

	struct epoll_event ev = {0};

	ev.events = EPOLLIN;
	ev.data.fd = 0; // stdin
	ret = epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, 0, &ev);
	if (ret < 0) {
		fprintf(stderr, "ERROR: epoll_ctl(stdin) failed '%i'\n", ret);
		return ret;
	}

	ev.events = EPOLLIN;
	ev.data.fd = s->listen_socket;
	ret = epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, s->listen_socket, &ev);
	if (ret < 0) {
		fprintf(stderr, "ERROR: epoll_ctl(listen_socket) failed '%i'\n",
		        ret);
		return ret;
	}

	return 0;
}

static int
init_all(struct ipc_server *s)
{
	// Yes we should be running.
	s->running = true;
	s->exit_on_disconnect = debug_get_bool_option_exit_on_disconnect();

	int ret = xrt_instance_create(&s->xinst);
	if (ret < 0) {
		teardown_all(s);
		return ret;
	}

	ret = xrt_instance_select(s->xinst, s->xdevs, IPC_SERVER_NUM_XDEVS);
	if (ret < 0) {
		teardown_all(s);
		return ret;
	}

	if (s->xdevs[0] == NULL) {
		teardown_all(s);
		return -1;
	}

	ret = xrt_instance_create_fd_compositor(s->xinst, s->xdevs[0], false,
	                                        &s->xcfd);
	if (ret < 0) {
		teardown_all(s);
		return ret;
	}

	ret = init_shm(s);
	if (ret < 0) {
		teardown_all(s);
		return ret;
	}

	ret = init_listen_socket(s);
	if (ret < 0) {
		teardown_all(s);
		return ret;
	}

	ret = init_epoll(s);
	if (ret < 0) {
		teardown_all(s);
		return ret;
	}

	// Easier to use.
	s->xc = &s->xcfd->base;

	u_var_add_root(s, "IPC Server", false);
	u_var_add_bool(s, &s->print_debug, "print.debug");
	u_var_add_bool(s, &s->print_spew, "print.spew");
	u_var_add_bool(s, &s->exit_on_disconnect, "exit_on_disconnect");
	u_var_add_bool(s, (void *)&s->running, "running");

	return 0;
}

static void
handle_listen(struct ipc_server *vs)
{
	int ret = accept(vs->listen_socket, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "ERROR: accept '%i'\n", ret);
		vs->running = false;
	}

	volatile struct ipc_client_state *cs = &vs->thread_state;

	// The return is the new fd;
	int fd = ret;

	if (vs->thread_started && !vs->thread_stopping) {
		fprintf(stderr, "ERROR: Client already connected!\n");
		close(fd);
		return;
	}

	if (vs->thread_stopping) {
		os_thread_join((struct os_thread *)&vs->thread);
		os_thread_destroy((struct os_thread *)&vs->thread);
		vs->thread_stopping = false;
	}

	vs->thread_started = true;
	cs->ipc_socket_fd = fd;
	os_thread_start((struct os_thread *)&vs->thread,
	                ipc_server_client_thread, (void *)cs);
}

#define NUM_POLL_EVENTS 8
#define NO_SLEEP 0

static void
check_epoll(struct ipc_server *vs)
{
	int epoll_fd = vs->epoll_fd;

	struct epoll_event events[NUM_POLL_EVENTS] = {0};

	// No sleeping, returns immediately.
	int ret = epoll_wait(epoll_fd, events, NUM_POLL_EVENTS, NO_SLEEP);
	if (ret < 0) {
		fprintf(stderr, "EPOLL ERROR! \"%i\"\n", ret);
		vs->running = false;
		return;
	}

	for (int i = 0; i < ret; i++) {
		// If we get data on stdin, stop.
		if (events[i].data.fd == 0) {
			vs->running = false;
			return;
		}

		// Somebody new at the door.
		if (events[i].data.fd == vs->listen_socket) {
			handle_listen(vs);
		}
	}
}

static void
set_rendering_state(volatile struct ipc_client_state *active_client,
                    struct comp_swapchain_image **l,
                    struct comp_swapchain_image **r,
                    bool *using_idle_images)
{
	// our ipc server thread will fill in l & r
	// swapchain indices and toggle wait to false
	// when the client calls end_frame, signalling
	// us to render.
	volatile struct ipc_render_state *render_state =
	    &active_client->render_state;

	if (!render_state->rendering) {
		return;
	}

	uint32_t li = render_state->l_swapchain_index;
	uint32_t ri = render_state->r_swapchain_index;
	struct comp_swapchain *cl = comp_swapchain(active_client->xscs[li]);
	struct comp_swapchain *cr = comp_swapchain(active_client->xscs[ri]);
	*l = &cl->images[render_state->l_image_index];
	*r = &cr->images[render_state->r_image_index];

	// set our client state back to waiting.
	render_state->rendering = false;

	// comp_compositor_garbage_collect(c);

	*using_idle_images = false;
}

static int
main_loop(struct ipc_server *vs)
{
	struct xrt_compositor *xc = vs->xc;
	struct comp_compositor *c = comp_compositor(xc);

	// make sure all our client connections have a handle to the compositor
	// and consistent initial state
	vs->thread_state.server = vs;
	vs->thread_state.xc = xc;

	struct comp_swapchain_image *last_l = NULL;
	struct comp_swapchain_image *last_r = NULL;

	bool using_idle_images = true;

	while (vs->running) {

		/*
		 * Check polling.
		 */
		check_epoll(vs);


		/*
		 * Update active client.
		 */

		volatile struct ipc_client_state *active_client = NULL;
		if (vs->thread_state.active) {
			active_client = &vs->thread_state;
		}


		/*
		 * Render the swapchains.
		 */

		struct comp_swapchain_image *l = NULL;
		struct comp_swapchain_image *r = NULL;

		if (active_client == NULL || !active_client->active ||
		    active_client->num_swapchains == 0) {
			if (!using_idle_images) {
				COMP_DEBUG(c, "Resetting to idle images.");
				comp_renderer_set_idle_images(c->r);
				using_idle_images = true;
				last_l = NULL;
				last_r = NULL;
			}
		} else {
			set_rendering_state(active_client, &l, &r,
			                    &using_idle_images);
		}

		// Rendering idle images
		if (l == NULL || r == NULL) {
			comp_renderer_frame_cached(c->r);
			comp_compositor_garbage_collect(c);
			continue;
		}

		// Rebuild command buffers if we are showing new buffers.
		if (last_l != l || last_r != r) {
			comp_renderer_reset(c->r);
		}
		last_l = l;
		last_r = r;

		comp_renderer_frame(c->r, l, 0, r, 0);

		// Now is a good time to destroy objects.
		comp_compositor_garbage_collect(c);
	}

	return 0;
}


/*
 *
 * Exported functions.
 *
 */

int
ipc_server_main(int argc, char **argv)
{
	struct ipc_server *s = U_TYPED_CALLOC(struct ipc_server);
	int ret = init_all(s);
	if (ret < 0) {
		free(s);
		return ret;
	}

	ret = main_loop(s);

	teardown_all(s);
	free(s);

	return ret;
}
