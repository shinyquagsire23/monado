// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Handling functions called from generated dispatch function.
 * @author Pete Black <pblack@collabora.com>
 * @ingroup ipc_server
 */

#include "xrt/xrt_gfx_native.h"

#include "util/u_misc.h"
#include "util/u_handles.h"
#include "util/u_trace_marker.h"

#include "server/ipc_server.h"
#include "ipc_server_generated.h"

#ifdef XRT_GRAPHICS_SYNC_HANDLE_IS_FD
#include <unistd.h>
#endif


/*
 *
 * Helper functions.
 *
 */

static xrt_result_t
validate_swapchain_state(volatile struct ipc_client_state *ics, uint32_t *out_index)
{
	// Our handle is just the index for now.
	uint32_t index = 0;
	for (; index < IPC_MAX_CLIENT_SWAPCHAINS; index++) {
		if (!ics->swapchain_data[index].active) {
			break;
		}
	}

	if (index >= IPC_MAX_CLIENT_SWAPCHAINS) {
		IPC_ERROR(ics->server, "Too many swapchains!");
		return XRT_ERROR_IPC_FAILURE;
	}

	*out_index = index;

	return XRT_SUCCESS;
}

static void
set_swapchain_info(volatile struct ipc_client_state *ics,
                   uint32_t index,
                   const struct xrt_swapchain_create_info *info,
                   struct xrt_swapchain *xsc)
{
	ics->xscs[index] = xsc;
	ics->swapchain_data[index].active = true;
	ics->swapchain_data[index].width = info->width;
	ics->swapchain_data[index].height = info->height;
	ics->swapchain_data[index].format = info->format;
	ics->swapchain_data[index].image_count = xsc->image_count;
}


/*
 *
 * Handle functions.
 *
 */

xrt_result_t
ipc_handle_instance_get_shm_fd(volatile struct ipc_client_state *ics,
                               uint32_t max_handle_capacity,
                               xrt_shmem_handle_t *out_handles,
                               uint32_t *out_handle_count)
{
	IPC_TRACE_MARKER();

	assert(max_handle_capacity >= 1);

	out_handles[0] = ics->server->ism_handle;
	*out_handle_count = 1;

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_compositor_get_info(volatile struct ipc_client_state *ics,
                                      struct xrt_system_compositor_info *out_info)
{
	IPC_TRACE_MARKER();

	*out_info = ics->server->xsysc->info;

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_session_create(volatile struct ipc_client_state *ics, const struct xrt_session_info *xsi)
{
	IPC_TRACE_MARKER();

	struct xrt_compositor_native *xcn = NULL;

	if (ics->xc != NULL) {
		return XRT_ERROR_IPC_SESSION_ALREADY_CREATED;
	}

	xrt_result_t xret = xrt_syscomp_create_native_compositor(ics->server->xsysc, xsi, &xcn);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	ics->client_state.session_overlay = xsi->is_overlay;
	ics->client_state.z_order = xsi->z_order;

	ics->xc = &xcn->base;

	xrt_syscomp_set_state(ics->server->xsysc, ics->xc, ics->client_state.session_visible,
	                      ics->client_state.session_focused);
	xrt_syscomp_set_z_order(ics->server->xsysc, ics->xc, ics->client_state.z_order);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_session_begin(volatile struct ipc_client_state *ics)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	return xrt_comp_begin_session(ics->xc, 0);
}

xrt_result_t
ipc_handle_session_end(volatile struct ipc_client_state *ics)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	return xrt_comp_end_session(ics->xc);
}

xrt_result_t
ipc_handle_session_destroy(volatile struct ipc_client_state *ics)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	ipc_server_client_destroy_compositor(ics);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_get_info(volatile struct ipc_client_state *ics, struct xrt_compositor_info *out_info)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	*out_info = ics->xc->info;

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_predict_frame(volatile struct ipc_client_state *ics,
                                    int64_t *out_frame_id,
                                    uint64_t *out_wake_up_time_ns,
                                    uint64_t *out_predicted_display_time_ns,
                                    uint64_t *out_predicted_display_period_ns)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	/*
	 * We use this to signal that the session has started, this is needed
	 * to make this client/session active/visible/focused.
	 */
	ipc_server_activate_session(ics);

	uint64_t gpu_time_ns = 0;
	return xrt_comp_predict_frame(        //
	    ics->xc,                          //
	    out_frame_id,                     //
	    out_wake_up_time_ns,              //
	    &gpu_time_ns,                     //
	    out_predicted_display_time_ns,    //
	    out_predicted_display_period_ns); //
}

xrt_result_t
ipc_handle_compositor_wait_woke(volatile struct ipc_client_state *ics, int64_t frame_id)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	return xrt_comp_mark_frame(ics->xc, frame_id, XRT_COMPOSITOR_FRAME_POINT_WOKE, os_monotonic_get_ns());
}

xrt_result_t
ipc_handle_compositor_begin_frame(volatile struct ipc_client_state *ics, int64_t frame_id)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	return xrt_comp_begin_frame(ics->xc, frame_id);
}

xrt_result_t
ipc_handle_compositor_discard_frame(volatile struct ipc_client_state *ics, int64_t frame_id)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	return xrt_comp_discard_frame(ics->xc, frame_id);
}

static bool
_update_projection_layer(struct xrt_compositor *xc,
                         volatile struct ipc_client_state *ics,
                         volatile struct ipc_layer_entry *layer,
                         uint32_t i)
{
	// xdev
	uint32_t device_id = layer->xdev_id;
	// left
	uint32_t lxsci = layer->swapchain_ids[0];
	// right
	uint32_t rxsci = layer->swapchain_ids[1];

	struct xrt_device *xdev = get_xdev(ics, device_id);
	struct xrt_swapchain *lxcs = ics->xscs[lxsci];
	struct xrt_swapchain *rxcs = ics->xscs[rxsci];

	if (lxcs == NULL || rxcs == NULL) {
		U_LOG_E("Invalid swap chain for projection layer!");
		return false;
	}

	if (xdev == NULL) {
		U_LOG_E("Invalid xdev for projection layer!");
		return false;
	}

	// Cast away volatile.
	struct xrt_layer_data *data = (struct xrt_layer_data *)&layer->data;

	xrt_comp_layer_stereo_projection(xc, xdev, lxcs, rxcs, data);

	return true;
}

static bool
_update_projection_layer_depth(struct xrt_compositor *xc,
                               volatile struct ipc_client_state *ics,
                               volatile struct ipc_layer_entry *layer,
                               uint32_t i)
{
	// xdev
	uint32_t xdevi = layer->xdev_id;
	// left
	uint32_t l_xsci = layer->swapchain_ids[0];
	// right
	uint32_t r_xsci = layer->swapchain_ids[1];
	// left
	uint32_t l_d_xsci = layer->swapchain_ids[2];
	// right
	uint32_t r_d_xsci = layer->swapchain_ids[3];

	struct xrt_device *xdev = get_xdev(ics, xdevi);
	struct xrt_swapchain *l_xcs = ics->xscs[l_xsci];
	struct xrt_swapchain *r_xcs = ics->xscs[r_xsci];
	struct xrt_swapchain *l_d_xcs = ics->xscs[l_d_xsci];
	struct xrt_swapchain *r_d_xcs = ics->xscs[r_d_xsci];

	if (l_xcs == NULL || r_xcs == NULL || l_d_xcs == NULL || r_d_xcs == NULL) {
		U_LOG_E("Invalid swap chain for projection layer #%u!", i);
		return false;
	}

	if (xdev == NULL) {
		U_LOG_E("Invalid xdev for projection layer #%u!", i);
		return false;
	}

	// Cast away volatile.
	struct xrt_layer_data *data = (struct xrt_layer_data *)&layer->data;

	xrt_comp_layer_stereo_projection_depth(xc, xdev, l_xcs, r_xcs, l_d_xcs, r_d_xcs, data);

	return true;
}

static bool
do_single(struct xrt_compositor *xc,
          volatile struct ipc_client_state *ics,
          volatile struct ipc_layer_entry *layer,
          uint32_t i,
          const char *name,
          struct xrt_device **out_xdev,
          struct xrt_swapchain **out_xcs,
          struct xrt_layer_data **out_data)
{
	uint32_t device_id = layer->xdev_id;
	uint32_t sci = layer->swapchain_ids[0];

	struct xrt_device *xdev = get_xdev(ics, device_id);
	struct xrt_swapchain *xcs = ics->xscs[sci];

	if (xcs == NULL) {
		U_LOG_E("Invalid swapchain for layer #%u, '%s'!", i, name);
		return false;
	}

	if (xdev == NULL) {
		U_LOG_E("Invalid xdev for layer #%u, '%s'!", i, name);
		return false;
	}

	// Cast away volatile.
	struct xrt_layer_data *data = (struct xrt_layer_data *)&layer->data;

	*out_xdev = xdev;
	*out_xcs = xcs;
	*out_data = data;

	return true;
}

static bool
_update_quad_layer(struct xrt_compositor *xc,
                   volatile struct ipc_client_state *ics,
                   volatile struct ipc_layer_entry *layer,
                   uint32_t i)
{
	struct xrt_device *xdev;
	struct xrt_swapchain *xcs;
	struct xrt_layer_data *data;

	if (!do_single(xc, ics, layer, i, "quad", &xdev, &xcs, &data)) {
		return false;
	}

	xrt_comp_layer_quad(xc, xdev, xcs, data);

	return true;
}

static bool
_update_cube_layer(struct xrt_compositor *xc,
                   volatile struct ipc_client_state *ics,
                   volatile struct ipc_layer_entry *layer,
                   uint32_t i)
{
	struct xrt_device *xdev;
	struct xrt_swapchain *xcs;
	struct xrt_layer_data *data;

	if (!do_single(xc, ics, layer, i, "cube", &xdev, &xcs, &data)) {
		return false;
	}

	xrt_comp_layer_cube(xc, xdev, xcs, data);

	return true;
}

static bool
_update_cylinder_layer(struct xrt_compositor *xc,
                       volatile struct ipc_client_state *ics,
                       volatile struct ipc_layer_entry *layer,
                       uint32_t i)
{
	struct xrt_device *xdev;
	struct xrt_swapchain *xcs;
	struct xrt_layer_data *data;

	if (!do_single(xc, ics, layer, i, "cylinder", &xdev, &xcs, &data)) {
		return false;
	}

	xrt_comp_layer_cylinder(xc, xdev, xcs, data);

	return true;
}

static bool
_update_equirect1_layer(struct xrt_compositor *xc,
                        volatile struct ipc_client_state *ics,
                        volatile struct ipc_layer_entry *layer,
                        uint32_t i)
{
	struct xrt_device *xdev;
	struct xrt_swapchain *xcs;
	struct xrt_layer_data *data;

	if (!do_single(xc, ics, layer, i, "equirect1", &xdev, &xcs, &data)) {
		return false;
	}

	xrt_comp_layer_equirect1(xc, xdev, xcs, data);

	return true;
}

static bool
_update_equirect2_layer(struct xrt_compositor *xc,
                        volatile struct ipc_client_state *ics,
                        volatile struct ipc_layer_entry *layer,
                        uint32_t i)
{
	struct xrt_device *xdev;
	struct xrt_swapchain *xcs;
	struct xrt_layer_data *data;

	if (!do_single(xc, ics, layer, i, "equirect2", &xdev, &xcs, &data)) {
		return false;
	}

	xrt_comp_layer_equirect2(xc, xdev, xcs, data);

	return true;
}

static bool
_update_layers(volatile struct ipc_client_state *ics, struct xrt_compositor *xc, struct ipc_layer_slot *slot)
{
	IPC_TRACE_MARKER();

	for (uint32_t i = 0; i < slot->layer_count; i++) {
		volatile struct ipc_layer_entry *layer = &slot->layers[i];

		switch (layer->data.type) {
		case XRT_LAYER_STEREO_PROJECTION:
			if (!_update_projection_layer(xc, ics, layer, i)) {
				return false;
			}
			break;
		case XRT_LAYER_STEREO_PROJECTION_DEPTH:
			if (!_update_projection_layer_depth(xc, ics, layer, i)) {
				return false;
			}
			break;
		case XRT_LAYER_QUAD:
			if (!_update_quad_layer(xc, ics, layer, i)) {
				return false;
			}
			break;
		case XRT_LAYER_CUBE:
			if (!_update_cube_layer(xc, ics, layer, i)) {
				return false;
			}
			break;
		case XRT_LAYER_CYLINDER:
			if (!_update_cylinder_layer(xc, ics, layer, i)) {
				return false;
			}
			break;
		case XRT_LAYER_EQUIRECT1:
			if (!_update_equirect1_layer(xc, ics, layer, i)) {
				return false;
			}
			break;
		case XRT_LAYER_EQUIRECT2:
			if (!_update_equirect2_layer(xc, ics, layer, i)) {
				return false;
			}
			break;
		default: U_LOG_E("Unhandled layer type '%i'!", layer->data.type); break;
		}
	}

	return true;
}

xrt_result_t
ipc_handle_compositor_layer_sync(volatile struct ipc_client_state *ics,
                                 int64_t frame_id,
                                 uint32_t slot_id,
                                 uint32_t *out_free_slot_id,
                                 const xrt_graphics_sync_handle_t *handles,
                                 const uint32_t handle_count)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	struct ipc_shared_memory *ism = ics->server->ism;
	struct ipc_layer_slot *slot = &ism->slots[slot_id];
	xrt_graphics_sync_handle_t sync_handle = XRT_GRAPHICS_SYNC_HANDLE_INVALID;

	// If we have one or more save the first handle.
	if (handle_count >= 1) {
		sync_handle = handles[0];
	}

	// Free all sync handles after the first one.
	for (uint32_t i = 1; i < handle_count; i++) {
		// Checks for valid handle.
		xrt_graphics_sync_handle_t tmp = handles[i];
		u_graphics_sync_unref(&tmp);
	}

	// Copy current slot data.
	struct ipc_layer_slot copy = *slot;


	/*
	 * Transfer data to underlying compositor.
	 */

	xrt_comp_layer_begin(ics->xc, frame_id, copy.display_time_ns, copy.env_blend_mode);

	_update_layers(ics, ics->xc, &copy);

	xrt_comp_layer_commit(ics->xc, frame_id, sync_handle);


	/*
	 * Manage shared state.
	 */

	os_mutex_lock(&ics->server->global_state.lock);

	*out_free_slot_id = (ics->server->current_slot_index + 1) % IPC_MAX_SLOTS;
	ics->server->current_slot_index = *out_free_slot_id;

	os_mutex_unlock(&ics->server->global_state.lock);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_layer_sync_with_semaphore(volatile struct ipc_client_state *ics,
                                                int64_t frame_id,
                                                uint32_t slot_id,
                                                uint32_t semaphore_id,
                                                uint64_t semaphore_value,
                                                uint32_t *out_free_slot_id)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}
	if (semaphore_id >= IPC_MAX_CLIENT_SEMAPHORES) {
		IPC_ERROR(ics->server, "Invalid semaphore_id");
		return XRT_ERROR_IPC_FAILURE;
	}
	if (ics->xcsems[semaphore_id] == NULL) {
		IPC_ERROR(ics->server, "Semaphore of id %u not created!", semaphore_id);
		return XRT_ERROR_IPC_FAILURE;
	}

	struct xrt_compositor_semaphore *xcsem = ics->xcsems[semaphore_id];

	struct ipc_shared_memory *ism = ics->server->ism;
	struct ipc_layer_slot *slot = &ism->slots[slot_id];

	// Copy current slot data.
	struct ipc_layer_slot copy = *slot;



	/*
	 * Transfer data to underlying compositor.
	 */

	xrt_comp_layer_begin(ics->xc, frame_id, copy.display_time_ns, copy.env_blend_mode);

	_update_layers(ics, ics->xc, &copy);

	xrt_comp_layer_commit_with_semaphore(ics->xc, frame_id, xcsem, semaphore_value);


	/*
	 * Manage shared state.
	 */

	os_mutex_lock(&ics->server->global_state.lock);

	*out_free_slot_id = (ics->server->current_slot_index + 1) % IPC_MAX_SLOTS;
	ics->server->current_slot_index = *out_free_slot_id;

	os_mutex_unlock(&ics->server->global_state.lock);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_poll_events(volatile struct ipc_client_state *ics, union xrt_compositor_event *out_xce)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	return xrt_comp_poll_events(ics->xc, out_xce);
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

	if (ics->imc.socket_fd <= 0) {
		return XRT_ERROR_IPC_FAILURE;
	}

	*out_client_desc = ics->client_state;
	out_client_desc->io_active = ics->io_active;

	//@todo: track this data in the ipc_client_state struct
	out_client_desc->primary_application = false;
	if (ics->server->global_state.active_client_index == (int)id) {
		out_client_desc->primary_application = true;
	}

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_set_client_info(volatile struct ipc_client_state *ics, const struct ipc_app_state *client_desc)
{
	ics->client_state.info = client_desc->info;
	ics->client_state.pid = client_desc->pid;

	IPC_INFO(ics->server,
	         "Client info\n"
	         "\tapplication_name: '%s'\n"
	         "\tpid: %i",
	         client_desc->info.application_name, //
	         client_desc->pid);                  //

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_get_clients(volatile struct ipc_client_state *_ics, struct ipc_client_list *list)
{
	for (uint32_t i = 0; i < IPC_MAX_CLIENTS; i++) {
		list->ids[i] = _ics->server->threads[i].ics.server_thread_index;
	}
	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_set_primary_client(volatile struct ipc_client_state *ics, uint32_t client_id)
{
	IPC_INFO(ics->server, "System setting active client to %d.", client_id);

	ipc_server_set_active_client(ics->server, client_id);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_set_focused_client(volatile struct ipc_client_state *ics, uint32_t client_id)
{
	IPC_INFO(ics->server, "UNIMPLEMENTED: system setting focused client to %d.", client_id);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_toggle_io_client(volatile struct ipc_client_state *_ics, uint32_t client_id)
{
	volatile struct ipc_client_state *ics = NULL;

	if (client_id >= IPC_MAX_CLIENTS) {
		return XRT_ERROR_IPC_FAILURE;
	}

	ics = &_ics->server->threads[client_id].ics;

	if (ics->imc.socket_fd <= 0) {
		return XRT_ERROR_IPC_FAILURE;
	}

	ics->io_active = !ics->io_active;

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_system_toggle_io_device(volatile struct ipc_client_state *ics, uint32_t device_id)
{
	if (device_id >= IPC_MAX_DEVICES) {
		return XRT_ERROR_IPC_FAILURE;
	}

	struct ipc_device *idev = &ics->server->idevs[device_id];

	idev->io_active = !idev->io_active;

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_swapchain_get_properties(volatile struct ipc_client_state *ics,
                                    const struct xrt_swapchain_create_info *info,
                                    struct xrt_swapchain_create_properties *xsccp)
{
	IPC_TRACE_MARKER();

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	return xrt_comp_get_swapchain_create_properties(ics->xc, info, xsccp);
}

xrt_result_t
ipc_handle_swapchain_create(volatile struct ipc_client_state *ics,
                            const struct xrt_swapchain_create_info *info,
                            uint32_t *out_id,
                            uint32_t *out_image_count,
                            uint64_t *out_size,
                            bool *out_use_dedicated_allocation,
                            uint32_t max_handle_capacity,
                            xrt_graphics_buffer_handle_t *out_handles,
                            uint32_t *out_handle_count)
{
	IPC_TRACE_MARKER();

	xrt_result_t xret = XRT_SUCCESS;
	uint32_t index = 0;

	xret = validate_swapchain_state(ics, &index);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	// Create the swapchain
	struct xrt_swapchain *xsc = NULL; // Has to be NULL.
	xret = xrt_comp_create_swapchain(ics->xc, info, &xsc);
	if (xret != XRT_SUCCESS) {
		if (xret == XRT_ERROR_SWAPCHAIN_FLAG_VALID_BUT_UNSUPPORTED) {
			IPC_WARN(ics->server,
			         "xrt_comp_create_swapchain: Attempted to create valid, but unsupported swapchain");
		} else {
			IPC_ERROR(ics->server, "Error xrt_comp_create_swapchain failed!");
		}
		return xret;
	}

	// It's now safe to increment the number of swapchains.
	ics->swapchain_count++;

	IPC_TRACE(ics->server, "Created swapchain %d.", index);

	set_swapchain_info(ics, index, info, xsc);

	// return our result to the caller.
	struct xrt_swapchain_native *xscn = (struct xrt_swapchain_native *)xsc;

	// Limit checking
	assert(xsc->image_count <= IPC_MAX_SWAPCHAIN_HANDLES);
	assert(xsc->image_count <= max_handle_capacity);

	for (size_t i = 1; i < xsc->image_count; i++) {
		assert(xscn->images[0].size == xscn->images[i].size);
		assert(xscn->images[0].use_dedicated_allocation == xscn->images[i].use_dedicated_allocation);
	}

	// Assuming all images allocated in the same swapchain have the same allocation requirements.
	*out_size = xscn->images[0].size;
	*out_use_dedicated_allocation = xscn->images[0].use_dedicated_allocation;
	*out_id = index;
	*out_image_count = xsc->image_count;

	// Setup the fds.
	*out_handle_count = xsc->image_count;
	for (size_t i = 0; i < xsc->image_count; i++) {
		out_handles[i] = xscn->images[i].handle;
	}

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_swapchain_import(volatile struct ipc_client_state *ics,
                            const struct xrt_swapchain_create_info *info,
                            const struct ipc_arg_swapchain_from_native *args,
                            uint32_t *out_id,
                            const xrt_graphics_buffer_handle_t *handles,
                            uint32_t handle_count)
{
	IPC_TRACE_MARKER();

	xrt_result_t xret = XRT_SUCCESS;
	uint32_t index = 0;

	xret = validate_swapchain_state(ics, &index);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	struct xrt_image_native xins[IPC_MAX_SWAPCHAIN_HANDLES] = {0};
	for (uint32_t i = 0; i < handle_count; i++) {
		xins[i].handle = handles[i];
		xins[i].size = args->sizes[i];
	}

	// create the swapchain
	struct xrt_swapchain *xsc = NULL;
	xret = xrt_comp_import_swapchain(ics->xc, info, xins, handle_count, &xsc);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	// It's now safe to increment the number of swapchains.
	ics->swapchain_count++;

	IPC_TRACE(ics->server, "Created swapchain %d.", index);

	set_swapchain_info(ics, index, info, xsc);
	*out_id = index;

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_swapchain_wait_image(volatile struct ipc_client_state *ics, uint32_t id, uint64_t timeout_ns, uint32_t index)
{
	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	//! @todo Look up the index.
	uint32_t sc_index = id;
	struct xrt_swapchain *xsc = ics->xscs[sc_index];

	xrt_swapchain_wait_image(xsc, timeout_ns, index);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_swapchain_acquire_image(volatile struct ipc_client_state *ics, uint32_t id, uint32_t *out_index)
{
	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	//! @todo Look up the index.
	uint32_t sc_index = id;
	struct xrt_swapchain *xsc = ics->xscs[sc_index];

	xrt_swapchain_acquire_image(xsc, out_index);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_swapchain_release_image(volatile struct ipc_client_state *ics, uint32_t id, uint32_t index)
{
	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	//! @todo Look up the index.
	uint32_t sc_index = id;
	struct xrt_swapchain *xsc = ics->xscs[sc_index];

	xrt_swapchain_release_image(xsc, index);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_swapchain_destroy(volatile struct ipc_client_state *ics, uint32_t id)
{
	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	ics->swapchain_count--;

	// Drop our reference, does NULL checking. Cast away volatile.
	xrt_swapchain_reference((struct xrt_swapchain **)&ics->xscs[id], NULL);
	ics->swapchain_data[id].active = false;

	return XRT_SUCCESS;
}


/*
 *
 * Compositor semaphore function..
 *
 */

xrt_result_t
ipc_handle_compositor_semaphore_create(volatile struct ipc_client_state *ics,
                                       uint32_t *out_id,
                                       uint32_t max_handle_count,
                                       xrt_graphics_sync_handle_t *out_handles,
                                       uint32_t *out_handle_count)
{
	xrt_result_t xret;

	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	int id = 0;
	for (; id < IPC_MAX_CLIENT_SEMAPHORES; id++) {
		if (ics->xcsems[id] == NULL) {
			break;
		}
	}

	if (id == IPC_MAX_CLIENT_SEMAPHORES) {
		IPC_ERROR(ics->server, "Too many compositor semaphores alive!");
		return XRT_ERROR_IPC_FAILURE;
	}

	struct xrt_compositor_semaphore *xcsem = NULL;
	xrt_graphics_sync_handle_t handle = XRT_GRAPHICS_SYNC_HANDLE_INVALID;

	xret = xrt_comp_create_semaphore(ics->xc, &handle, &xcsem);
	if (xret != XRT_SUCCESS) {
		IPC_ERROR(ics->server, "Failed to create compositor semaphore!");
		return xret;
	}

	// Set it directly, no need to use reference here.
	ics->xcsems[id] = xcsem;

	// Set out parameters.
	*out_id = id;
	out_handles[0] = handle;
	*out_handle_count = 1;

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_compositor_semaphore_destroy(volatile struct ipc_client_state *ics, uint32_t id)
{
	if (ics->xc == NULL) {
		return XRT_ERROR_IPC_SESSION_NOT_CREATED;
	}

	if (ics->xcsems[id] == NULL) {
		IPC_ERROR(ics->server, "Client tried to delete non-existent compositor semaphore!");
		return XRT_ERROR_IPC_FAILURE;
	}

	ics->compositor_semaphore_count--;

	// Drop our reference, does NULL checking. Cast away volatile.
	xrt_compositor_semaphore_reference((struct xrt_compositor_semaphore **)&ics->xcsems[id], NULL);

	return XRT_SUCCESS;
}


/*
 *
 * Device functions.
 *
 */

xrt_result_t
ipc_handle_device_update_input(volatile struct ipc_client_state *ics, uint32_t id)
{
	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct ipc_shared_memory *ism = ics->server->ism;
	struct ipc_device *idev = get_idev(ics, device_id);
	struct xrt_device *xdev = idev->xdev;
	struct ipc_shared_device *isdev = &ism->isdevs[device_id];

	// Update inputs.
	xrt_device_update_inputs(xdev);

	// Copy data into the shared memory.
	struct xrt_input *src = xdev->inputs;
	struct xrt_input *dst = &ism->inputs[isdev->first_input_index];
	size_t size = sizeof(struct xrt_input) * isdev->input_count;

	bool io_active = ics->io_active && idev->io_active;
	if (io_active) {
		memcpy(dst, src, size);
	} else {
		memset(dst, 0, size);

		for (uint32_t i = 0; i < isdev->input_count; i++) {
			dst[i].name = src[i].name;

			// Special case the rotation of the head.
			if (dst[i].name == XRT_INPUT_GENERIC_HEAD_POSE) {
				dst[i].active = src[i].active;
			}
		}
	}

	// Reply.
	return XRT_SUCCESS;
}

static struct xrt_input *
find_input(volatile struct ipc_client_state *ics, uint32_t device_id, enum xrt_input_name name)
{
	struct ipc_shared_memory *ism = ics->server->ism;
	struct ipc_shared_device *isdev = &ism->isdevs[device_id];
	struct xrt_input *io = &ism->inputs[isdev->first_input_index];

	for (uint32_t i = 0; i < isdev->input_count; i++) {
		if (io[i].name == name) {
			return &io[i];
		}
	}

	return NULL;
}

xrt_result_t
ipc_handle_device_get_tracked_pose(volatile struct ipc_client_state *ics,
                                   uint32_t id,
                                   enum xrt_input_name name,
                                   uint64_t at_timestamp,
                                   struct xrt_space_relation *out_relation)
{
	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct ipc_device *isdev = &ics->server->idevs[device_id];
	struct xrt_device *xdev = isdev->xdev;

	// Find the input
	struct xrt_input *input = find_input(ics, device_id, name);
	if (input == NULL) {
		return XRT_ERROR_IPC_FAILURE;
	}

	// Special case the headpose.
	bool disabled = (!isdev->io_active || !ics->io_active) && name != XRT_INPUT_GENERIC_HEAD_POSE;
	bool active_on_client = input->active;

	// We have been disabled but the client hasn't called update.
	if (disabled && active_on_client) {
		U_ZERO(out_relation);
		return XRT_SUCCESS;
	}

	if (disabled || !active_on_client) {
		return XRT_ERROR_POSE_NOT_ACTIVE;
	}

	// Get the pose.
	xrt_device_get_tracked_pose(xdev, name, at_timestamp, out_relation);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_device_get_hand_tracking(volatile struct ipc_client_state *ics,
                                    uint32_t id,
                                    enum xrt_input_name name,
                                    uint64_t at_timestamp,
                                    struct xrt_hand_joint_set *out_value,
                                    uint64_t *out_timestamp)
{

	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct xrt_device *xdev = get_xdev(ics, device_id);

	// Get the pose.
	xrt_device_get_hand_tracking(xdev, name, at_timestamp, out_value, out_timestamp);

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_device_get_view_poses_2(volatile struct ipc_client_state *ics,
                                   uint32_t id,
                                   const struct xrt_vec3 *default_eye_relation,
                                   uint64_t at_timestamp_ns,
                                   struct ipc_info_get_view_poses_2 *out_info)
{
	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct xrt_device *xdev = get_xdev(ics, device_id);

	xrt_device_get_view_poses(    //
	    xdev,                     //
	    default_eye_relation,     //
	    at_timestamp_ns,          //
	    2,                        //
	    &out_info->head_relation, //
	    out_info->fovs,           //
	    out_info->poses);         //

	return XRT_SUCCESS;
}

xrt_result_t
ipc_handle_device_set_output(volatile struct ipc_client_state *ics,
                             uint32_t id,
                             enum xrt_output_name name,
                             const union xrt_output_value *value)
{
	// To make the code a bit more readable.
	uint32_t device_id = id;
	struct xrt_device *xdev = get_xdev(ics, device_id);

	// Set the output.
	xrt_device_set_output(xdev, name, value);

	return XRT_SUCCESS;
}
