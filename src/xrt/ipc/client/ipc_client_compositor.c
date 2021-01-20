// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Client side wrapper of compositor.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_client
 */

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_defines.h"

#include "util/u_misc.h"

#include "os/os_time.h"

#include "shared/ipc_protocol.h"
#include "client/ipc_client.h"
#include "ipc_client_generated.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <assert.h>

#ifdef XRT_GRAPHICS_SYNC_HANDLE_IS_FD
#include <unistd.h>
#endif


/*
 *
 * Internal structs and helpers.
 *
 */

//! Define to test the loopback allocator.
#undef IPC_USE_LOOPBACK_IMAGE_ALLOCATOR

/*!
 * Client proxy for an xrt_compositor_native implementation over IPC.
 * @implements xrt_compositor_native
 */
struct ipc_client_compositor
{
	struct xrt_compositor_native base;

	//! Should be turned into it's own object.
	struct xrt_system_compositor system;

	struct ipc_connection *ipc_c;

	//! Optional image allocator.
	struct xrt_image_native_allocator *xina;

	struct
	{
		//! Id that we are currently using for submitting layers.
		uint32_t slot_id;

		uint32_t num_layers;

		enum xrt_blend_mode env_blend_mode;
	} layers;

	//! Has the native compositor been created, only supports one for now.
	bool compositor_created;

#ifdef IPC_USE_LOOPBACK_IMAGE_ALLOCATOR
	//! To test image allocator.
	struct xrt_image_native_allocator loopback_xina;
#endif
};

/*!
 * Client proxy for an xrt_swapchain_native implementation over IPC.
 * @implements xrt_swapchain_native
 */
struct ipc_client_swapchain
{
	struct xrt_swapchain_native base;

	struct ipc_client_compositor *icc;

	uint32_t id;
};

static inline struct ipc_client_compositor *
ipc_client_compositor(struct xrt_compositor *xc)
{
	return (struct ipc_client_compositor *)xc;
}

static inline struct ipc_client_swapchain *
ipc_client_swapchain(struct xrt_swapchain *xs)
{
	return (struct ipc_client_swapchain *)xs;
}


/*
 *
 * Misc functions
 *
 */

void
compositor_disconnect(struct ipc_connection *ipc_c)
{
	ipc_message_channel_close(&ipc_c->imc);
}

#define IPC_CALL_CHK(call)                                                                                             \
	xrt_result_t res = (call);                                                                                     \
	if (res == XRT_ERROR_IPC_FAILURE) {                                                                            \
		IPC_ERROR(icc->ipc_c, "Call error '%s'!", __func__);                                                   \
	}

static xrt_result_t
get_info(struct xrt_compositor *xc, struct xrt_compositor_info *out_info)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_CALL_CHK(ipc_call_compositor_get_info(icc->ipc_c, out_info));

	return res;
}

static xrt_result_t
get_system_info(struct ipc_client_compositor *icc, struct xrt_system_compositor_info *out_info)
{
	IPC_CALL_CHK(ipc_call_system_compositor_get_info(icc->ipc_c, out_info));

	return res;
}


/*
 *
 * Swapchain.
 *
 */

static void
ipc_compositor_swapchain_destroy(struct xrt_swapchain *xsc)
{
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);
	struct ipc_client_compositor *icc = ics->icc;

	IPC_CALL_CHK(ipc_call_swapchain_destroy(icc->ipc_c, ics->id));

	free(xsc);
}

static xrt_result_t
ipc_compositor_swapchain_wait_image(struct xrt_swapchain *xsc, uint64_t timeout, uint32_t index)
{
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);
	struct ipc_client_compositor *icc = ics->icc;

	IPC_CALL_CHK(ipc_call_swapchain_wait_image(icc->ipc_c, ics->id, timeout, index));

	return res;
}

static xrt_result_t
ipc_compositor_swapchain_acquire_image(struct xrt_swapchain *xsc, uint32_t *out_index)
{
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);
	struct ipc_client_compositor *icc = ics->icc;

	IPC_CALL_CHK(ipc_call_swapchain_acquire_image(icc->ipc_c, ics->id, out_index));

	return res;
}

static xrt_result_t
ipc_compositor_swapchain_release_image(struct xrt_swapchain *xsc, uint32_t index)
{
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);
	struct ipc_client_compositor *icc = ics->icc;

	IPC_CALL_CHK(ipc_call_swapchain_release_image(icc->ipc_c, ics->id, index));

	return res;
}


/*
 *
 * Compositor functions.
 *
 */


static xrt_result_t
swapchain_server_create(struct ipc_client_compositor *icc,
                        const struct xrt_swapchain_create_info *info,
                        struct xrt_swapchain **out_xsc)
{
	xrt_graphics_buffer_handle_t remote_handles[IPC_MAX_SWAPCHAIN_HANDLES] = {0};
	xrt_result_t r = XRT_SUCCESS;
	uint32_t handle;
	uint32_t num_images;
	uint64_t size;

	r = ipc_call_swapchain_create(icc->ipc_c,                 // connection
	                              info,                       // in
	                              &handle,                    // out
	                              &num_images,                // out
	                              &size,                      // out
	                              remote_handles,             // handles
	                              IPC_MAX_SWAPCHAIN_HANDLES); // handles
	if (r != XRT_SUCCESS) {
		return r;
	}

	struct ipc_client_swapchain *ics = U_TYPED_CALLOC(struct ipc_client_swapchain);
	ics->base.base.num_images = num_images;
	ics->base.base.wait_image = ipc_compositor_swapchain_wait_image;
	ics->base.base.acquire_image = ipc_compositor_swapchain_acquire_image;
	ics->base.base.release_image = ipc_compositor_swapchain_release_image;
	ics->base.base.destroy = ipc_compositor_swapchain_destroy;
	ics->icc = icc;
	ics->id = handle;

	for (uint32_t i = 0; i < num_images; i++) {
		ics->base.images[i].handle = remote_handles[i];
		ics->base.images[i].size = size;
	}

	*out_xsc = &ics->base.base;

	return XRT_SUCCESS;
}

static xrt_result_t
swapchain_server_import(struct ipc_client_compositor *icc,
                        const struct xrt_swapchain_create_info *info,
                        struct xrt_image_native *native_images,
                        uint32_t num_images,
                        struct xrt_swapchain **out_xsc)
{
	struct ipc_arg_swapchain_from_native args = {0};
	xrt_graphics_buffer_handle_t handles[IPC_MAX_SWAPCHAIN_HANDLES] = {0};
	xrt_result_t r = XRT_SUCCESS;
	uint32_t id = 0;

	for (uint32_t i = 0; i < num_images; i++) {
		handles[i] = native_images[i].handle;
		args.sizes[i] = native_images[i].size;
	}

	// This does not consume the handles, it copies them.
	r = ipc_call_swapchain_import(icc->ipc_c, // connection
	                              info,       // in
	                              &args,      // in
	                              handles,    // handles
	                              num_images, // handles
	                              &id);       // out
	if (r != XRT_SUCCESS) {
		return r;
	}

	struct ipc_client_swapchain *ics = U_TYPED_CALLOC(struct ipc_client_swapchain);
	ics->base.base.num_images = num_images;
	ics->base.base.wait_image = ipc_compositor_swapchain_wait_image;
	ics->base.base.acquire_image = ipc_compositor_swapchain_acquire_image;
	ics->base.base.release_image = ipc_compositor_swapchain_release_image;
	ics->base.base.destroy = ipc_compositor_swapchain_destroy;
	ics->icc = icc;
	ics->id = id;

	// The handles where copied in the IPC call so we can reuse them here.
	for (uint32_t i = 0; i < num_images; i++) {
		ics->base.images[i] = native_images[i];
	}

	*out_xsc = &ics->base.base;

	return XRT_SUCCESS;
}

static xrt_result_t
swapchain_allocator_create(struct ipc_client_compositor *icc,
                           struct xrt_image_native_allocator *xina,
                           const struct xrt_swapchain_create_info *info,
                           struct xrt_swapchain **out_xsc)
{
	struct xrt_image_native images[3];
	uint32_t num_images = (uint32_t)ARRAY_SIZE(images);
	xrt_result_t xret;

	if ((info->create & XRT_SWAPCHAIN_CREATE_STATIC_IMAGE) != 0) {
		num_images = 1;
	}

	xret = xrt_images_allocate(xina, info, num_images, images);
	if (xret != XRT_SUCCESS) {
		return xret;
	}

	/*
	 * The import function takes ownership of the handles,
	 * we do not need free them if the call succeeds.
	 */
	xret = swapchain_server_import(icc, info, images, num_images, out_xsc);
	if (xret != XRT_SUCCESS) {
		xrt_images_free(xina, num_images, images);
	}

	return xret;
}

static xrt_result_t
ipc_compositor_swapchain_create(struct xrt_compositor *xc,
                                const struct xrt_swapchain_create_info *info,
                                struct xrt_swapchain **out_xsc)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);
	struct xrt_image_native_allocator *xina = icc->xina;
	xrt_result_t r;

	if (xina == NULL) {
		r = swapchain_server_create(icc, info, out_xsc);
	} else {
		r = swapchain_allocator_create(icc, xina, info, out_xsc);
	}

	return r;
}

static xrt_result_t
ipc_compositor_swapchain_import(struct xrt_compositor *xc,
                                const struct xrt_swapchain_create_info *info,
                                struct xrt_image_native *native_images,
                                uint32_t num_images,
                                struct xrt_swapchain **out_xsc)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);
	return swapchain_server_import(icc, info, native_images, num_images, out_xsc);
}

static xrt_result_t
ipc_compositor_poll_events(struct xrt_compositor *xc, union xrt_compositor_event *out_xce)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_TRACE(icc->ipc_c, "Polling for events.");

	IPC_CALL_CHK(ipc_call_compositor_poll_events(icc->ipc_c, out_xce));

	return res;
}

static xrt_result_t
ipc_compositor_begin_session(struct xrt_compositor *xc, enum xrt_view_type view_type)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_TRACE(icc->ipc_c, "Compositor begin session.");

	IPC_CALL_CHK(ipc_call_session_begin(icc->ipc_c));

	return res;
}

static xrt_result_t
ipc_compositor_end_session(struct xrt_compositor *xc)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_TRACE(icc->ipc_c, "Compositor end session.");

	IPC_CALL_CHK(ipc_call_session_end(icc->ipc_c));

	return res;
}

static xrt_result_t
ipc_compositor_wait_frame(struct xrt_compositor *xc,
                          int64_t *out_frame_id,
                          uint64_t *out_predicted_display_time,
                          uint64_t *out_predicted_display_period)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	uint64_t wake_up_time_ns = 0;
	uint64_t min_display_period_ns = 0;

	IPC_CALL_CHK(ipc_call_compositor_wait_frame(icc->ipc_c,                   // Connection
	                                            out_frame_id,                 // Frame id
	                                            out_predicted_display_time,   // Display time
	                                            &wake_up_time_ns,             // When we should wake up
	                                            out_predicted_display_period, // Current period
	                                            &min_display_period_ns));     // Minimum display period

	uint64_t now_ns = os_monotonic_get_ns();

	// Lets hope its not to late.
	if (wake_up_time_ns <= now_ns) {
		res = ipc_call_compositor_wait_woke(icc->ipc_c, *out_frame_id);
		return res;
	}

	const uint64_t _1ms_in_ns = 1000 * 1000;
	const uint64_t measured_scheduler_latency_ns = 50 * 1000;

	// Within one ms, just release the app right now.
	if (wake_up_time_ns - _1ms_in_ns <= now_ns) {
		res = ipc_call_compositor_wait_woke(icc->ipc_c, *out_frame_id);
		return res;
	}

	// This is how much we should sleep.
	uint64_t diff_ns = wake_up_time_ns - now_ns;

	// A minor tweak that helps hit the time better.
	diff_ns -= measured_scheduler_latency_ns;

	os_nanosleep(diff_ns);

	res = ipc_call_compositor_wait_woke(icc->ipc_c, *out_frame_id);

#if 0
	uint64_t then_ns = now_ns;
	now_ns = os_monotonic_get_ns();

	diff_ns = now_ns - then_ns;
	uint64_t ms100 = diff_ns / (1000 * 10);

	U_LOG_D("%s: Slept %i.%02ims", __func__, (int)ms100 / 100,
	        (int)ms100 % 100);
#endif

	return res;
}

static xrt_result_t
ipc_compositor_begin_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_CALL_CHK(ipc_call_compositor_begin_frame(icc->ipc_c, frame_id));

	return res;
}

static xrt_result_t
ipc_compositor_layer_begin(struct xrt_compositor *xc, int64_t frame_id, enum xrt_blend_mode env_blend_mode)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	icc->layers.env_blend_mode = env_blend_mode;

	return XRT_SUCCESS;
}

static xrt_result_t
ipc_compositor_layer_stereo_projection(struct xrt_compositor *xc,
                                       struct xrt_device *xdev,
                                       struct xrt_swapchain *l_xsc,
                                       struct xrt_swapchain *r_xsc,
                                       const struct xrt_layer_data *data)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	assert(data->type == XRT_LAYER_STEREO_PROJECTION);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];
	struct ipc_layer_entry *layer = &slot->layers[icc->layers.num_layers];
	struct ipc_client_swapchain *l = ipc_client_swapchain(l_xsc);
	struct ipc_client_swapchain *r = ipc_client_swapchain(r_xsc);

	layer->xdev_id = 0; //! @todo Real id.
	layer->swapchain_ids[0] = l->id;
	layer->swapchain_ids[1] = r->id;
	layer->swapchain_ids[2] = -1;
	layer->swapchain_ids[3] = -1;
	layer->data = *data;

	// Increment the number of layers.
	icc->layers.num_layers++;

	return XRT_SUCCESS;
}

static xrt_result_t
ipc_compositor_layer_stereo_projection_depth(struct xrt_compositor *xc,
                                             struct xrt_device *xdev,
                                             struct xrt_swapchain *l_xsc,
                                             struct xrt_swapchain *r_xsc,
                                             struct xrt_swapchain *l_d_xsc,
                                             struct xrt_swapchain *r_d_xsc,
                                             const struct xrt_layer_data *data)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	assert(data->type == XRT_LAYER_STEREO_PROJECTION_DEPTH);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];
	struct ipc_layer_entry *layer = &slot->layers[icc->layers.num_layers];
	struct ipc_client_swapchain *l = ipc_client_swapchain(l_xsc);
	struct ipc_client_swapchain *r = ipc_client_swapchain(r_xsc);
	struct ipc_client_swapchain *l_d = ipc_client_swapchain(l_d_xsc);
	struct ipc_client_swapchain *r_d = ipc_client_swapchain(r_d_xsc);

	layer->xdev_id = 0; //! @todo Real id.
	layer->swapchain_ids[0] = l->id;
	layer->swapchain_ids[1] = r->id;
	layer->swapchain_ids[2] = l_d->id;
	layer->swapchain_ids[3] = r_d->id;
	layer->data = *data;

	// Increment the number of layers.
	icc->layers.num_layers++;

	return XRT_SUCCESS;
}

static xrt_result_t
handle_layer(struct xrt_compositor *xc,
             struct xrt_device *xdev,
             struct xrt_swapchain *xsc,
             const struct xrt_layer_data *data,
             enum xrt_layer_type type)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	assert(data->type == type);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];
	struct ipc_layer_entry *layer = &slot->layers[icc->layers.num_layers];
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);

	layer->xdev_id = 0; //! @todo Real id.
	layer->swapchain_ids[0] = ics->id;
	layer->swapchain_ids[1] = -1;
	layer->swapchain_ids[2] = -1;
	layer->swapchain_ids[3] = -1;
	layer->data = *data;

	// Increment the number of layers.
	icc->layers.num_layers++;

	return XRT_SUCCESS;
}

static xrt_result_t
ipc_compositor_layer_quad(struct xrt_compositor *xc,
                          struct xrt_device *xdev,
                          struct xrt_swapchain *xsc,
                          const struct xrt_layer_data *data)
{
	return handle_layer(xc, xdev, xsc, data, XRT_LAYER_QUAD);
}

static xrt_result_t
ipc_compositor_layer_cube(struct xrt_compositor *xc,
                          struct xrt_device *xdev,
                          struct xrt_swapchain *xsc,
                          const struct xrt_layer_data *data)
{
	return handle_layer(xc, xdev, xsc, data, XRT_LAYER_CUBE);
}

static xrt_result_t
ipc_compositor_layer_cylinder(struct xrt_compositor *xc,
                              struct xrt_device *xdev,
                              struct xrt_swapchain *xsc,
                              const struct xrt_layer_data *data)
{
	return handle_layer(xc, xdev, xsc, data, XRT_LAYER_CYLINDER);
}

static xrt_result_t
ipc_compositor_layer_equirect1(struct xrt_compositor *xc,
                               struct xrt_device *xdev,
                               struct xrt_swapchain *xsc,
                               const struct xrt_layer_data *data)
{
	return handle_layer(xc, xdev, xsc, data, XRT_LAYER_EQUIRECT1);
}

static xrt_result_t
ipc_compositor_layer_equirect2(struct xrt_compositor *xc,
                               struct xrt_device *xdev,
                               struct xrt_swapchain *xsc,
                               const struct xrt_layer_data *data)
{
	return handle_layer(xc, xdev, xsc, data, XRT_LAYER_EQUIRECT2);
}

static xrt_result_t
ipc_compositor_layer_commit(struct xrt_compositor *xc, int64_t frame_id, xrt_graphics_sync_handle_t sync_handle)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	bool valid_sync = xrt_graphics_sync_handle_is_valid(sync_handle);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];

	// Last bit of data to put in the shared memory area.
	slot->num_layers = icc->layers.num_layers;

	IPC_CALL_CHK(ipc_call_compositor_layer_sync( //
	    icc->ipc_c,                              //
	    frame_id,                                //
	    icc->layers.slot_id,                     //
	    &sync_handle,                            //
	    valid_sync ? 1 : 0,                      //
	    &icc->layers.slot_id));                  //

	// Reset.
	icc->layers.num_layers = 0;

#ifdef XRT_GRAPHICS_SYNC_HANDLE_IS_FD
	// Need to consume this handle.
	if (valid_sync) {
		close(sync_handle);
		sync_handle = XRT_GRAPHICS_SYNC_HANDLE_INVALID;
	}
#else
#error "Not yet implemented for this platform"
#endif

	return res;
}

static xrt_result_t
ipc_compositor_discard_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_CALL_CHK(ipc_call_compositor_discard_frame(icc->ipc_c, frame_id));

	return res;
}

static void
ipc_compositor_destroy(struct xrt_compositor *xc)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	assert(icc->compositor_created);

	icc->compositor_created = false;
}


/*
 *
 * Loopback image allocator.
 *
 */

#ifdef IPC_USE_LOOPBACK_IMAGE_ALLOCATOR
static inline xrt_result_t
ipc_compositor_images_allocate(struct xrt_image_native_allocator *xina,
                               const struct xrt_swapchain_create_info *xsci,
                               size_t in_num_images,
                               struct xrt_image_native *out_images)
{
	struct ipc_client_compositor *icc = container_of(xina, struct ipc_client_compositor, loopback_xina);

	int remote_fds[IPC_MAX_SWAPCHAIN_FDS] = {0};
	xrt_result_t r = XRT_SUCCESS;
	uint32_t num_images;
	uint32_t handle;
	uint64_t size;

	for (size_t i = 0; i < ARRAY_SIZE(remote_fds); i++) {
		remote_fds[i] = -1;
	}

	for (size_t i = 0; i < in_num_images; i++) {
		out_images[i].fd = -1;
		out_images[i].size = 0;
	}

	r = ipc_call_swapchain_create(icc->ipc_c,             // connection
	                              xsci,                   // in
	                              &handle,                // out
	                              &num_images,            // out
	                              &size,                  // out
	                              remote_fds,             // fds
	                              IPC_MAX_SWAPCHAIN_FDS); // fds
	if (r != XRT_SUCCESS) {
		return r;
	}

	/*
	 * It's okay to destroy it immediately, the native handles are
	 * now owned by us and we keep the buffers alive that way.
	 */
	r = ipc_call_swapchain_destroy(icc->ipc_c, handle);
	assert(r == XRT_SUCCESS);

	// Clumsy way of handling this.
	if (num_images < in_num_images) {
		for (uint32_t k = 0; k < num_images && k < in_num_images; k++) {
			/*
			 * Paranoia, we do know that any fd not touched by
			 * ipc_call_swapchain_create will be -1.
			 */
			if (remote_fds[k] >= 0) {
				close(remote_fds[k]);
				remote_fds[k] = -1;
			}
		}

		return XRT_ERROR_IPC_FAILURE;
	}

	// Copy up to in_num_images, or num_images what ever is lowest.
	uint32_t i = 0;
	for (; i < num_images && i < in_num_images; i++) {
		out_images[i].fd = remote_fds[i];
		out_images[i].size = size;
	}

	// Close any fds we are not interested in.
	for (; i < num_images; i++) {
		/*
		 * Paranoia, we do know that any fd not touched by
		 * ipc_call_swapchain_create will be -1.
		 */
		if (remote_fds[i] >= 0) {
			close(remote_fds[i]);
			remote_fds[i] = -1;
		}
	}

	return XRT_SUCCESS;
}

static inline xrt_result_t
ipc_compositor_images_free(struct xrt_image_native_allocator *xina,
                           size_t num_images,
                           struct xrt_image_native *out_images)
{
	for (uint32_t i = 0; i < num_images; i++) {
		close(out_images[i].fd);
		out_images[i].fd = -1;
		out_images[i].size = 0;
	}

	return XRT_SUCCESS;
}

static inline void
ipc_compositor_images_destroy(struct xrt_image_native_allocator *xina)
{
	// Noop
}
#endif


/*
 *
 * System compositor.
 *
 */

xrt_result_t
ipc_syscomp_create_native_compositor(struct xrt_system_compositor *xsc,
                                     const struct xrt_session_info *xsi,
                                     struct xrt_compositor_native **out_xcn)
{
	struct ipc_client_compositor *icc = container_of(xsc, struct ipc_client_compositor, system);

	if (icc->compositor_created) {
		return XRT_ERROR_MULTI_SESSION_NOT_IMPLEMENTED;
	}

	icc->compositor_created = true;
	*out_xcn = &icc->base;

	IPC_CALL_CHK(ipc_call_session_create(icc->ipc_c, xsi));

	return XRT_SUCCESS;
}

void
ipc_syscomp_destroy(struct xrt_system_compositor *xsc)
{
	struct ipc_client_compositor *icc = container_of(xsc, struct ipc_client_compositor, system);

	// Does null checking.
	xrt_images_destroy(&icc->xina);

	//! @todo Implement
	IPC_TRACE(icc->ipc_c, "NOT IMPLEMENTED compositor destroy.");

	free(icc);
}


/*
 *
 * 'Exported' functions.
 *
 */

int
ipc_client_create_system_compositor(struct ipc_connection *ipc_c,
                                    struct xrt_image_native_allocator *xina,
                                    struct xrt_device *xdev,
                                    struct xrt_system_compositor **out_xcs)
{
	struct ipc_client_compositor *c = U_TYPED_CALLOC(struct ipc_client_compositor);

	c->base.base.create_swapchain = ipc_compositor_swapchain_create;
	c->base.base.import_swapchain = ipc_compositor_swapchain_import;
	c->base.base.begin_session = ipc_compositor_begin_session;
	c->base.base.end_session = ipc_compositor_end_session;
	c->base.base.wait_frame = ipc_compositor_wait_frame;
	c->base.base.begin_frame = ipc_compositor_begin_frame;
	c->base.base.discard_frame = ipc_compositor_discard_frame;
	c->base.base.layer_begin = ipc_compositor_layer_begin;
	c->base.base.layer_stereo_projection = ipc_compositor_layer_stereo_projection;
	c->base.base.layer_stereo_projection_depth = ipc_compositor_layer_stereo_projection_depth;
	c->base.base.layer_quad = ipc_compositor_layer_quad;
	c->base.base.layer_cube = ipc_compositor_layer_cube;
	c->base.base.layer_cylinder = ipc_compositor_layer_cylinder;
	c->base.base.layer_equirect1 = ipc_compositor_layer_equirect1;
	c->base.base.layer_equirect2 = ipc_compositor_layer_equirect2;
	c->base.base.layer_commit = ipc_compositor_layer_commit;
	c->base.base.destroy = ipc_compositor_destroy;
	c->base.base.poll_events = ipc_compositor_poll_events;
	c->system.create_native_compositor = ipc_syscomp_create_native_compositor;
	c->system.destroy = ipc_syscomp_destroy;
	c->ipc_c = ipc_c;
	c->xina = xina;


#ifdef IPC_USE_LOOPBACK_IMAGE_ALLOCATOR
	c->loopback_xina.images_allocate = ipc_compositor_images_allocate;
	c->loopback_xina.images_free = ipc_compositor_images_free;
	c->loopback_xina.destroy = ipc_compositor_images_destroy;

	if (c->xina == NULL) {
		c->xina = &c->loopback_xina;
	}
#endif

	// Fetch info from the compositor, among it the format format list.
	get_info(&(c->base.base), &c->base.base.info);

	// Fetch info from the system compositor.
	get_system_info(c, &c->system.info);

	*out_xcs = &c->system;

	return 0;
}
