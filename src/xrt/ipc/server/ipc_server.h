// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common server side code.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_server
 */

#pragma once

#include "xrt/xrt_compiler.h"

#include "util/u_render_timing.h"
#include "util/u_logging.h"

#include "os/os_threading.h"

#include "shared/ipc_protocol.h"
#include "shared/ipc_utils.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *
 * Logging
 *
 */

#define IPC_TRACE(d, ...) U_LOG_IFL_T(d->ll, __VA_ARGS__)
#define IPC_DEBUG(d, ...) U_LOG_IFL_D(d->ll, __VA_ARGS__)
#define IPC_INFO(d, ...) U_LOG_IFL_I(d->ll, __VA_ARGS__)
#define IPC_WARN(d, ...) U_LOG_IFL_W(d->ll, __VA_ARGS__)
#define IPC_ERROR(d, ...) U_LOG_IFL_E(d->ll, __VA_ARGS__)

/*
 *
 * Structs
 *
 */

#define IPC_SERVER_NUM_XDEVS 8
#define IPC_MAX_CLIENT_SWAPCHAINS 32
//#define IPC_MAX_CLIENTS 8

struct xrt_instance;
struct xrt_compositor;
struct xrt_compositor_native;


/*!
 * Information about a single swapchain.
 *
 * @ingroup ipc_server
 */
struct ipc_swapchain_data
{
	uint32_t width;
	uint32_t height;
	uint64_t format;
	uint32_t num_images;

	bool active;
};


struct ipc_queued_event
{
	bool pending;
	uint64_t timestamp;
	union xrt_compositor_event event;
};

/*!
 * Holds the state for a single client.
 *
 * @ingroup ipc_server
 */
struct ipc_client_state
{
	//! Link back to the main server.
	struct ipc_server *server;

	//! Compositor for this client.
	struct xrt_compositor *xc;

	//! Is the inputs and outputs active.
	bool io_active;

	//! Number of swapchains in use by client
	uint32_t num_swapchains;

	//! Ptrs to the swapchains
	struct xrt_swapchain *xscs[IPC_MAX_CLIENT_SWAPCHAINS];

	//! Data for the swapchains.
	struct ipc_swapchain_data swapchain_data[IPC_MAX_CLIENT_SWAPCHAINS];

	//! Socket fd used for client comms
	struct ipc_message_channel imc;

	//! State for rendering.
	struct ipc_layer_slot render_state;

	//! Whether we are currently rendering @ref render_state
	bool rendering_state;

	//! The frame timing state.
	struct u_rt_helper urth;

	struct ipc_app_state client_state;
	struct ipc_queued_event queued_events[IPC_EVENT_QUEUE_SIZE];

	int server_thread_index;
};

enum ipc_thread_state
{
	IPC_THREAD_READY,
	IPC_THREAD_STARTING,
	IPC_THREAD_RUNNING,
	IPC_THREAD_STOPPING,
};

struct ipc_thread
{
	struct os_thread thread;
	volatile enum ipc_thread_state state;
	volatile struct ipc_client_state ics;
};


/*!
 *
 */
struct ipc_device
{
	//! The actual device.
	struct xrt_device *xdev;

	//! Is the IO suppressed for this device.
	bool io_active;
};

/*!
 * Main IPC object for the server.
 *
 * @ingroup ipc_server
 */
struct ipc_server
{
	struct xrt_instance *xinst;

	/* ---- HACK ---- */
	void *hack;
	/* ---- HACK ---- */


	//! System compositor.
	struct xrt_system_compositor *xsysc;
	//! Native compositor.
	struct xrt_compositor_native *xcn;

	struct ipc_device idevs[IPC_SERVER_NUM_XDEVS];
	struct xrt_tracking_origin *xtracks[IPC_SERVER_NUM_XDEVS];

	struct ipc_shared_memory *ism;
	xrt_shmem_handle_t ism_handle;

	//! Socket that we accept connections on.
	int listen_socket;

	//! For waiting on various events in the main thread.
	int epoll_fd;

	// Is the mainloop supposed to run.
	volatile bool running;

	// Should we exit when a client disconnects.
	bool exit_on_disconnect;

	//! Were we launched by socket activation, instead of explicitly?
	bool launched_by_socket;

	//! The socket filename we bound to, if any.
	char *socket_filename;

	enum u_logging_level ll;

	struct ipc_thread threads[IPC_MAX_CLIENTS];

	volatile uint32_t current_slot_index;

	int active_client_index;
	int last_active_client_index;
	struct os_mutex global_state_lock;
};

/*!
 * Main entrypoint to the compositor process.
 *
 * @ingroup ipc_server
 */
int
ipc_server_main(int argc, char **argv);

/*!
 * Android entry point to the IPC server process.
 *
 * @ingroup ipc_server
 */
#ifdef XRT_OS_ANDROID
int
ipc_server_main_android(int fd);
#endif

/*!
 * Called by client threads to manage global state
 *
 * @ingroup ipc_server
 */
void
update_server_state(struct ipc_server *vs);

/*!
 * Thread function for the client side dispatching.
 *
 * @ingroup ipc_server
 */
void *
ipc_server_client_thread(void *_cs);


/*
 *
 * Helpers
 *
 */

/*!
 * Get a xdev with the given device_id.
 */
static inline struct xrt_device *
get_xdev(volatile struct ipc_client_state *ics, uint32_t device_id)
{
	return ics->server->idevs[device_id].xdev;
}

/*!
 * Get a idev with the given device_id.
 */
static inline struct ipc_device *
get_idev(volatile struct ipc_client_state *ics, uint32_t device_id)
{
	return &ics->server->idevs[device_id];
}


#ifdef __cplusplus
}
#endif
