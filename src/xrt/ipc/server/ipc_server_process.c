// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Server process functions.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_server
 */

#include "xrt/xrt_device.h"
#include "xrt/xrt_system.h"
#include "xrt/xrt_instance.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_os.h"

#include "os/os_time.h"
#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_trace_marker.h"
#include "util/u_verify.h"
#include "util/u_process.h"

#include "util/u_git_tag.h"

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
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ---- HACK ---- */
extern int
oxr_sdl2_hack_create(void **out_hack);

extern void
oxr_sdl2_hack_start(void *hack, struct xrt_instance *xinst, struct xrt_system_devices *xsysd);

extern void
oxr_sdl2_hack_stop(void **hack_ptr);
/* ---- HACK ---- */


/*
 *
 * Defines and helpers.
 *
 */

DEBUG_GET_ONCE_BOOL_OPTION(exit_on_disconnect, "IPC_EXIT_ON_DISCONNECT", false)
DEBUG_GET_ONCE_LOG_OPTION(ipc_log, "IPC_LOG", U_LOGGING_WARN)


/*
 *
 * Idev functions.
 *
 */

static int32_t
find_xdev_index(struct ipc_server *s, struct xrt_device *xdev)
{
	if (xdev == NULL) {
		return -1;
	}

	for (int32_t i = 0; i < XRT_SYSTEM_MAX_DEVICES; i++) {
		if (s->xsysd->xdevs[i] == xdev) {
			return i;
		}
	}

	IPC_WARN(s, "Could not find index for xdev: '%s'", xdev->str);

	return -1;
}

static void
init_idev(struct ipc_device *idev, struct xrt_device *xdev)
{
	if (xdev != NULL) {
		idev->io_active = true;
		idev->xdev = xdev;
	} else {
		idev->io_active = false;
	}
}

static void
teardown_idev(struct ipc_device *idev)
{
	idev->io_active = false;
}

static int
init_idevs(struct ipc_server *s)
{
	// Copy the devices over into the idevs array.
	for (size_t i = 0; i < XRT_SYSTEM_MAX_DEVICES; i++) {
		if (s->xsysd->xdevs[i] == NULL) {
			continue;
		}

		init_idev(&s->idevs[i], s->xsysd->xdevs[i]);
	}

	return 0;
}

static void
teardown_idevs(struct ipc_server *s)
{
	for (size_t i = 0; i < XRT_SYSTEM_MAX_DEVICES; i++) {
		teardown_idev(&s->idevs[i]);
	}
}


/*
 *
 * Static functions.
 *
 */

static void
teardown_all(struct ipc_server *s)
{
	u_var_remove_root(s);

	xrt_syscomp_destroy(&s->xsysc);

	teardown_idevs(s);

	xrt_system_devices_destroy(&s->xsysd);

	xrt_instance_destroy(&s->xinst);

	ipc_server_mainloop_deinit(&s->ml);

	os_mutex_destroy(&s->global_state.lock);
	u_process_destroy(s->process);
}

static int
init_tracking_origins(struct ipc_server *s)
{
	for (size_t i = 0; i < XRT_SYSTEM_MAX_DEVICES; i++) {
		struct xrt_device *xdev = s->idevs[i].xdev;
		if (xdev == NULL) {
			continue;
		}

		struct xrt_tracking_origin *xtrack = xdev->tracking_origin;
		assert(xtrack != NULL);
		size_t index = 0;

		for (; index < XRT_SYSTEM_MAX_DEVICES; index++) {
			if (s->xtracks[index] == NULL) {
				s->xtracks[index] = xtrack;
				break;
			}
			if (s->xtracks[index] == xtrack) {
				break;
			}
		}
	}

	return 0;
}

static void
handle_binding(struct ipc_shared_memory *ism,
               struct xrt_binding_profile *xbp,
               struct ipc_shared_binding_profile *isbp,
               uint32_t *input_pair_index_ptr,
               uint32_t *output_pair_index_ptr)
{
	uint32_t input_pair_index = *input_pair_index_ptr;
	uint32_t output_pair_index = *output_pair_index_ptr;

	isbp->name = xbp->name;

	// Copy the initial state and also count the number in input_pairs.
	size_t input_pair_start = input_pair_index;
	for (size_t k = 0; k < xbp->input_count; k++) {
		ism->input_pairs[input_pair_index++] = xbp->inputs[k];
	}

	// Setup the 'offsets' and number of input_pairs.
	if (input_pair_start != input_pair_index) {
		isbp->input_count = input_pair_index - input_pair_start;
		isbp->first_input_index = input_pair_start;
	}

	// Copy the initial state and also count the number in outputs.
	size_t output_pair_start = output_pair_index;
	for (size_t k = 0; k < xbp->output_count; k++) {
		ism->output_pairs[output_pair_index++] = xbp->outputs[k];
	}

	// Setup the 'offsets' and number of output_pairs.
	if (output_pair_start != output_pair_index) {
		isbp->output_count = output_pair_index - output_pair_start;
		isbp->first_output_index = output_pair_start;
	}

	*input_pair_index_ptr = input_pair_index;
	*output_pair_index_ptr = output_pair_index;
}

static int
init_shm(struct ipc_server *s)
{
	const size_t size = sizeof(struct ipc_shared_memory);
	xrt_shmem_handle_t handle;
	xrt_result_t result = ipc_shmem_create(size, &handle, (void **)&s->ism);
	if (result != XRT_SUCCESS) {
		return -1;
	}

	// we have a filehandle, we will pass this to our client
	s->ism_handle = handle;


	/*
	 *
	 * Setup the shared memory state.
	 *
	 */

	uint32_t count = 0;
	struct ipc_shared_memory *ism = s->ism;

	ism->startup_timestamp = os_monotonic_get_ns();

	// Setup the tracking origins.
	count = 0;
	for (size_t i = 0; i < XRT_SYSTEM_MAX_DEVICES; i++) {
		struct xrt_tracking_origin *xtrack = s->xtracks[i];
		if (xtrack == NULL) {
			continue;
		}

		// The position of the tracking origin matches that in the
		// server's memory.
		assert(i < XRT_SYSTEM_MAX_DEVICES);

		struct ipc_shared_tracking_origin *itrack = &ism->itracks[count++];
		memcpy(itrack->name, xtrack->name, sizeof(itrack->name));
		itrack->type = xtrack->type;
		itrack->offset = xtrack->offset;
	}

	ism->itrack_count = count;

	count = 0;
	uint32_t input_index = 0;
	uint32_t output_index = 0;
	uint32_t binding_index = 0;
	uint32_t input_pair_index = 0;
	uint32_t output_pair_index = 0;

	for (size_t i = 0; i < XRT_SYSTEM_MAX_DEVICES; i++) {
		struct xrt_device *xdev = s->idevs[i].xdev;
		if (xdev == NULL) {
			continue;
		}

		struct ipc_shared_device *isdev = &ism->isdevs[count++];

		isdev->name = xdev->name;
		memcpy(isdev->str, xdev->str, sizeof(isdev->str));

		isdev->orientation_tracking_supported = xdev->orientation_tracking_supported;
		isdev->position_tracking_supported = xdev->position_tracking_supported;
		isdev->device_type = xdev->device_type;
		isdev->hand_tracking_supported = xdev->hand_tracking_supported;
		isdev->force_feedback_supported = xdev->force_feedback_supported;

		// Is this a HMD?
		if (xdev->hmd != NULL) {
			ism->hmd.views[0].display.w_pixels = xdev->hmd->views[0].display.w_pixels;
			ism->hmd.views[0].display.h_pixels = xdev->hmd->views[0].display.h_pixels;
			ism->hmd.views[1].display.w_pixels = xdev->hmd->views[1].display.w_pixels;
			ism->hmd.views[1].display.h_pixels = xdev->hmd->views[1].display.h_pixels;

			for (size_t i = 0; i < xdev->hmd->blend_mode_count; i++) {
				// Not super necessary, we also do this assert in oxr_system.c
				assert(u_verify_blend_mode_valid(xdev->hmd->blend_modes[i]));
				ism->hmd.blend_modes[i] = xdev->hmd->blend_modes[i];
			}
			ism->hmd.blend_mode_count = xdev->hmd->blend_mode_count;
		}

		// Setup the tracking origin.
		isdev->tracking_origin_index = (uint32_t)-1;
		for (size_t k = 0; k < XRT_SYSTEM_MAX_DEVICES; k++) {
			if (xdev->tracking_origin != s->xtracks[k]) {
				continue;
			}

			isdev->tracking_origin_index = k;
			break;
		}

		assert(isdev->tracking_origin_index != (uint32_t)-1);

		// Initial update.
		xrt_device_update_inputs(xdev);

		// Bindings
		size_t binding_start = binding_index;
		for (size_t k = 0; k < xdev->binding_profile_count; k++) {
			handle_binding(ism, &xdev->binding_profiles[k], &ism->binding_profiles[binding_index++],
			               &input_pair_index, &output_pair_index);
		}

		// Setup the 'offsets' and number of bindings.
		if (binding_start != binding_index) {
			isdev->binding_profile_count = binding_index - binding_start;
			isdev->first_binding_profile_index = binding_start;
		}

		// Copy the initial state and also count the number in inputs.
		size_t input_start = input_index;
		for (size_t k = 0; k < xdev->input_count; k++) {
			ism->inputs[input_index++] = xdev->inputs[k];
		}

		// Setup the 'offsets' and number of inputs.
		if (input_start != input_index) {
			isdev->input_count = input_index - input_start;
			isdev->first_input_index = input_start;
		}

		// Copy the initial state and also count the number in outputs.
		size_t output_start = output_index;
		for (size_t k = 0; k < xdev->output_count; k++) {
			ism->outputs[output_index++] = xdev->outputs[k];
		}

		// Setup the 'offsets' and number of outputs.
		if (output_start != output_index) {
			isdev->output_count = output_index - output_start;
			isdev->first_output_index = output_start;
		}
	}

	// Finally tell the client how many devices we have.
	s->ism->isdev_count = count;

	// Assign all of the roles.
	ism->roles.head = find_xdev_index(s, s->xsysd->roles.head);
	ism->roles.left = find_xdev_index(s, s->xsysd->roles.left);
	ism->roles.right = find_xdev_index(s, s->xsysd->roles.right);
	ism->roles.gamepad = find_xdev_index(s, s->xsysd->roles.gamepad);
	ism->roles.hand_tracking.left = find_xdev_index(s, s->xsysd->roles.hand_tracking.left);
	ism->roles.hand_tracking.right = find_xdev_index(s, s->xsysd->roles.hand_tracking.right);

	// Fill out git version info.
	snprintf(s->ism->u_git_tag, IPC_VERSION_NAME_LEN, "%s", u_git_tag);

	return 0;
}

void
ipc_server_handle_failure(struct ipc_server *vs)
{
	// Right now handled just the same as a graceful shutdown.
	vs->running = false;
}

void
ipc_server_handle_shutdown_signal(struct ipc_server *vs)
{
	vs->running = false;
}

void
ipc_server_start_client_listener_thread(struct ipc_server *vs, int fd)
{
	volatile struct ipc_client_state *ics = NULL;
	int32_t cs_index = -1;

	os_mutex_lock(&vs->global_state.lock);

	// find the next free thread in our array (server_thread_index is -1)
	// and have it handle this connection
	for (uint32_t i = 0; i < IPC_MAX_CLIENTS; i++) {
		volatile struct ipc_client_state *_cs = &vs->threads[i].ics;
		if (_cs->server_thread_index < 0) {
			ics = _cs;
			cs_index = i;
			break;
		}
	}
	if (ics == NULL) {
		close(fd);

		// Unlock when we are done.
		os_mutex_unlock(&vs->global_state.lock);

		U_LOG_E("Max client count reached!");
		return;
	}

	struct ipc_thread *it = &vs->threads[cs_index];
	if (it->state != IPC_THREAD_READY && it->state != IPC_THREAD_STOPPING) {
		// we should not get here
		close(fd);

		// Unlock when we are done.
		os_mutex_unlock(&vs->global_state.lock);

		U_LOG_E("Client state management error!");
		return;
	}

	if (it->state != IPC_THREAD_READY) {
		os_thread_join(&it->thread);
		os_thread_destroy(&it->thread);
		it->state = IPC_THREAD_READY;
	}

	it->state = IPC_THREAD_STARTING;
	ics->imc.socket_fd = fd;
	ics->server = vs;
	ics->server_thread_index = cs_index;
	ics->io_active = true;
	os_thread_start(&it->thread, ipc_server_client_thread, (void *)ics);

	// Unlock when we are done.
	os_mutex_unlock(&vs->global_state.lock);
}

static int
init_all(struct ipc_server *s)
{
	xrt_result_t xret;
	int ret;

	s->process = u_process_create_if_not_running();

	if (!s->process) {
		U_LOG_E("monado-service is already running! Use XRT_LOG=trace for more information.");
		teardown_all(s);
		return 1;
	}

	// Yes we should be running.
	s->running = true;
	s->exit_on_disconnect = debug_get_bool_option_exit_on_disconnect();
	s->log_level = debug_get_log_option_ipc_log();

	xret = xrt_instance_create(NULL, &s->xinst);
	if (xret != XRT_SUCCESS) {
		IPC_ERROR(s, "Failed to create instance!");
		teardown_all(s);
		return -1;
	}

	xret = xrt_instance_create_system(s->xinst, &s->xsysd, &s->xsysc);
	if (xret != XRT_SUCCESS) {
		IPC_ERROR(s, "Could not create system!");
		teardown_all(s);
		return -1;
	}

	ret = init_idevs(s);
	if (ret < 0) {
		IPC_ERROR(s, "Failed to init idevs!");
		teardown_all(s);
		return ret;
	}

	ret = init_tracking_origins(s);
	if (ret < 0) {
		IPC_ERROR(s, "Failed to init tracking origins!");
		teardown_all(s);
		return ret;
	}

	ret = init_shm(s);
	if (ret < 0) {
		IPC_ERROR(s, "Could not init shared memory!");
		teardown_all(s);
		return ret;
	}

	ret = ipc_server_mainloop_init(&s->ml);
	if (ret < 0) {
		IPC_ERROR(s, "Failed to init ipc main loop!");
		teardown_all(s);
		return ret;
	}

	ret = os_mutex_init(&s->global_state.lock);
	if (ret < 0) {
		IPC_ERROR(s, "Global state lock mutex failed to inti!");
		teardown_all(s);
		return ret;
	}

	u_var_add_root(s, "IPC Server", false);
	u_var_add_log_level(s, &s->log_level, "Log level");
	u_var_add_bool(s, &s->exit_on_disconnect, "exit_on_disconnect");
	u_var_add_bool(s, (void *)&s->running, "running");

	return 0;
}

static int
main_loop(struct ipc_server *s)
{
	while (s->running) {
		os_nanosleep(U_TIME_1S_IN_NS / 20);

		// Check polling.
		ipc_server_mainloop_poll(s, &s->ml);
	}

	return 0;
}

static void
init_server_state(struct ipc_server *s)
{
	// set up initial state for global vars, and each client state

	s->global_state.active_client_index = -1; // we start off with no active client.
	s->global_state.last_active_client_index = -1;
	s->current_slot_index = 0;

	for (uint32_t i = 0; i < IPC_MAX_CLIENTS; i++) {
		volatile struct ipc_client_state *ics = &s->threads[i].ics;
		ics->server = s;
		ics->server_thread_index = -1;
	}
}


/*
 *
 * Client management functions.
 *
 */

static void
handle_overlay_client_events(volatile struct ipc_client_state *ics, int active_id, int prev_active_id)
{
	// Is an overlay session?
	if (!ics->client_state.session_overlay) {
		return;
	}

	// Does this client have a compositor yet, if not return?
	if (ics->xc == NULL) {
		return;
	}

	// Switch between main applications
	if (active_id >= 0 && prev_active_id >= 0) {
		xrt_syscomp_set_main_app_visibility(ics->server->xsysc, ics->xc, false);
		xrt_syscomp_set_main_app_visibility(ics->server->xsysc, ics->xc, true);
	}

	// Switch from idle to active application
	if (active_id >= 0 && prev_active_id < 0) {
		xrt_syscomp_set_main_app_visibility(ics->server->xsysc, ics->xc, true);
	}

	// Switch from active application to idle
	if (active_id < 0 && prev_active_id >= 0) {
		xrt_syscomp_set_main_app_visibility(ics->server->xsysc, ics->xc, false);
	}
}

static void
handle_focused_client_events(volatile struct ipc_client_state *ics, int active_id, int prev_active_id)
{
	// Set start z_order at the bottom.
	int64_t z_order = INT64_MIN;

	// Set visibility/focus to false on all applications.
	bool focused = false;
	bool visible = false;

	// Set visible + focused if we are the primary application
	if (ics->server_thread_index == active_id) {
		visible = true;
		focused = true;
		z_order = INT64_MIN;
	}

	// Set all overlays to always active and focused.
	if (ics->client_state.session_overlay) {
		visible = true;
		focused = true;
		z_order = ics->client_state.z_order;
	}

	ics->client_state.session_visible = visible;
	ics->client_state.session_focused = focused;
	ics->client_state.z_order = z_order;

	if (ics->xc != NULL) {
		xrt_syscomp_set_state(ics->server->xsysc, ics->xc, visible, focused);
		xrt_syscomp_set_z_order(ics->server->xsysc, ics->xc, z_order);
	}
}

static void
flush_state_to_all_clients_locked(struct ipc_server *s)
{
	for (uint32_t i = 0; i < IPC_MAX_CLIENTS; i++) {
		volatile struct ipc_client_state *ics = &s->threads[i].ics;

		// Not running?
		if (ics->server_thread_index < 0) {
			continue;
		}

		handle_focused_client_events(ics, s->global_state.active_client_index,
		                             s->global_state.last_active_client_index);
		handle_overlay_client_events(ics, s->global_state.active_client_index,
		                             s->global_state.last_active_client_index);
	}
}

static void
update_server_state_locked(struct ipc_server *s)
{
	// if our client that is set to active is still active,
	// and it is the same as our last active client, we can
	// early-out, as no events need to be sent

	if (s->global_state.active_client_index >= 0) {

		volatile struct ipc_client_state *ics = &s->threads[s->global_state.active_client_index].ics;

		if (ics->client_state.session_active &&
		    s->global_state.active_client_index == s->global_state.last_active_client_index) {
			return;
		}
	}


	// our active application has changed - this would typically be
	// switched by the monado-ctl application or other app making a
	// 'set active application' ipc call, or it could be a
	// connection loss resulting in us needing to 'fall through' to
	// the first active application
	//, or finally to the idle 'wallpaper' images.


	bool set_idle = true;
	int fallback_active_application = -1;

	// do we have a fallback application?
	for (uint32_t i = 0; i < IPC_MAX_CLIENTS; i++) {
		volatile struct ipc_client_state *ics = &s->threads[i].ics;
		if (ics->client_state.session_overlay == false && ics->server_thread_index >= 0 &&
		    ics->client_state.session_active) {
			fallback_active_application = i;
			set_idle = false;
		}
	}

	// if our currently-set active primary application is not
	// actually active/displayable, use the fallback application
	// instead.
	volatile struct ipc_client_state *ics = &s->threads[s->global_state.active_client_index].ics;
	if (!(ics->client_state.session_overlay == false && s->global_state.active_client_index >= 0 &&
	      ics->client_state.session_active)) {
		s->global_state.active_client_index = fallback_active_application;
	}


	// if we have no applications to fallback to, enable the idle
	// wallpaper.
	if (set_idle) {
		s->global_state.active_client_index = -1;
	}

	flush_state_to_all_clients_locked(s);

	s->global_state.last_active_client_index = s->global_state.active_client_index;
}

static void
set_active_client_locked(struct ipc_server *s, int client_id)
{
	if (client_id != s->global_state.active_client_index) {
		s->global_state.active_client_index = client_id;
	}
}

/*
 *
 * Exported functions.
 *
 */

void
ipc_server_set_active_client(struct ipc_server *s, int client_id)
{
	os_mutex_lock(&s->global_state.lock);
	set_active_client_locked(s, client_id);
	os_mutex_unlock(&s->global_state.lock);
}

void
ipc_server_activate_session(volatile struct ipc_client_state *ics)
{
	struct ipc_server *s = ics->server;

	// Already active, noop.
	if (ics->client_state.session_active) {
		return;
	}

	assert(ics->server_thread_index >= 0);

	// Multiple threads could call this at the same time.
	os_mutex_lock(&s->global_state.lock);

	ics->client_state.session_active = true;

	if (ics->client_state.session_overlay) {
		// For new active overlay sessions only update this session.
		handle_focused_client_events(ics, s->global_state.active_client_index,
		                             s->global_state.last_active_client_index);
		handle_overlay_client_events(ics, s->global_state.active_client_index,
		                             s->global_state.last_active_client_index);
	} else {
		// Update active client
		set_active_client_locked(s, ics->server_thread_index);
		// For new active regular sessions update all clients.
		update_server_state_locked(s);
	}

	os_mutex_unlock(&s->global_state.lock);
}

void
ipc_server_deactivate_session(volatile struct ipc_client_state *ics)
{
	struct ipc_server *s = ics->server;

	// Multiple threads could call this at the same time.
	os_mutex_lock(&s->global_state.lock);

	ics->client_state.session_active = false;

	update_server_state_locked(s);

	os_mutex_unlock(&s->global_state.lock);
}

void
ipc_server_update_state(struct ipc_server *s)
{
	// Multiple threads could call this at the same time.
	os_mutex_lock(&s->global_state.lock);

	update_server_state_locked(s);

	os_mutex_unlock(&s->global_state.lock);
}

#ifndef XRT_OS_ANDROID
int
ipc_server_main(int argc, char **argv)
{
	struct ipc_server *s = U_TYPED_CALLOC(struct ipc_server);

	U_LOG_I("Monado Service %s starting up...", u_git_tag);

	/* ---- HACK ---- */
	// need to create early before any vars are added
	oxr_sdl2_hack_create(&s->hack);
	/* ---- HACK ---- */

	int ret = init_all(s);
	if (ret < 0) {
		free(s->hack);
		free(s);
		return ret;
	}

	init_server_state(s);

	/* ---- HACK ---- */
	oxr_sdl2_hack_start(s->hack, s->xinst, s->xsysd);
	/* ---- HACK ---- */

	ret = main_loop(s);

	/* ---- HACK ---- */
	oxr_sdl2_hack_stop(&s->hack);
	/* ---- HACK ---- */

	teardown_all(s);
	free(s);

	U_LOG_I("Server exiting: '%i'!", ret);

	return ret;
}

#endif // !XRT_OS_ANDROID

#ifdef XRT_OS_ANDROID
int
ipc_server_main_android(struct ipc_server **ps, void (*startup_complete_callback)(void *data), void *data)
{
	struct ipc_server *s = U_TYPED_CALLOC(struct ipc_server);
	U_LOG_D("Created IPC server!");

	int ret = init_all(s);
	if (ret < 0) {
		free(s);
		return ret;
	}

	init_server_state(s);

	*ps = s;
	startup_complete_callback(data);

	ret = main_loop(s);

	teardown_all(s);
	free(s);

	U_LOG_I("Server exiting '%i'!", ret);

	return ret;
}
#endif // XRT_OS_ANDROID
