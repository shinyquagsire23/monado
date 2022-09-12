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
#include "xrt/xrt_config_os.h"

#include "os/os_time.h"

#include "util/u_wait.h"
#include "util/u_misc.h"
#include "util/u_trace_marker.h"

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

	//! Should be turned into its own object.
	struct xrt_system_compositor system;

	struct ipc_connection *ipc_c;

	//! Optional image allocator.
	struct xrt_image_native_allocator *xina;

	struct
	{
		//! Id that we are currently using for submitting layers.
		uint32_t slot_id;

		uint32_t layer_count;
	} layers;

	//! Has the native compositor been created, only supports one for now.
	bool compositor_created;

	//! To get better wake up in wait frame.
	struct os_precise_sleeper sleeper;

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

/*!
 * Client proxy for an xrt_compositor_semaphore implementation over IPC.
 * @implements xrt_compositor_semaphore
 */
struct ipc_client_compositor_semaphore
{
	struct xrt_compositor_semaphore base;

	struct ipc_client_compositor *icc;

	uint32_t id;
};


/*
 *
 * Helper functions.
 *
 */

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

static inline struct ipc_client_compositor_semaphore *
ipc_client_compositor_semaphore(struct xrt_compositor_semaphore *xcsem)
{
	return (struct ipc_client_compositor_semaphore *)xcsem;
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
	if (res != XRT_SUCCESS) {                                                                                      \
		IPC_ERROR(icc->ipc_c, "Call error '%i'!", res);                                                        \
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
ipc_compositor_swapchain_wait_image(struct xrt_swapchain *xsc, uint64_t timeout_ns, uint32_t index)
{
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);
	struct ipc_client_compositor *icc = ics->icc;

	IPC_CALL_CHK(ipc_call_swapchain_wait_image(icc->ipc_c, ics->id, timeout_ns, index));

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
 * Compositor semaphore functions.
 *
 */

static xrt_result_t
ipc_client_compositor_semaphore_wait(struct xrt_compositor_semaphore *xcsem, uint64_t value, uint64_t timeout_ns)
{
	struct ipc_client_compositor_semaphore *iccs = ipc_client_compositor_semaphore(xcsem);
	struct ipc_client_compositor *icc = iccs->icc;

	IPC_ERROR(icc->ipc_c, "Can not call wait on client side!");

	return XRT_ERROR_IPC_FAILURE;
}

static void
ipc_client_compositor_semaphore_destroy(struct xrt_compositor_semaphore *xcsem)
{
	struct ipc_client_compositor_semaphore *iccs = ipc_client_compositor_semaphore(xcsem);
	struct ipc_client_compositor *icc = iccs->icc;

	IPC_CALL_CHK(ipc_call_compositor_semaphore_destroy(icc->ipc_c, iccs->id));

	free(iccs);
}


/*
 *
 * Compositor functions.
 *
 */

static xrt_result_t
ipc_compositor_get_swapchain_create_properties(struct xrt_compositor *xc,
                                               const struct xrt_swapchain_create_info *info,
                                               struct xrt_swapchain_create_properties *xsccp)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_CALL_CHK(ipc_call_swapchain_get_properties(icc->ipc_c, info, xsccp));

	return res;
}

static xrt_result_t
swapchain_server_create(struct ipc_client_compositor *icc,
                        const struct xrt_swapchain_create_info *info,
                        struct xrt_swapchain **out_xsc)
{
	xrt_graphics_buffer_handle_t remote_handles[IPC_MAX_SWAPCHAIN_HANDLES] = {0};
	xrt_result_t r = XRT_SUCCESS;
	uint32_t handle;
	uint32_t image_count;
	uint64_t size;
	bool use_dedicated_allocation;

	r = ipc_call_swapchain_create(icc->ipc_c,                 // connection
	                              info,                       // in
	                              &handle,                    // out
	                              &image_count,               // out
	                              &size,                      // out
	                              &use_dedicated_allocation,  // out
	                              remote_handles,             // handles
	                              IPC_MAX_SWAPCHAIN_HANDLES); // handles
	if (r != XRT_SUCCESS) {
		return r;
	}

	struct ipc_client_swapchain *ics = U_TYPED_CALLOC(struct ipc_client_swapchain);
	ics->base.base.image_count = image_count;
	ics->base.base.wait_image = ipc_compositor_swapchain_wait_image;
	ics->base.base.acquire_image = ipc_compositor_swapchain_acquire_image;
	ics->base.base.release_image = ipc_compositor_swapchain_release_image;
	ics->base.base.destroy = ipc_compositor_swapchain_destroy;
	ics->base.base.reference.count = 1;
	ics->icc = icc;
	ics->id = handle;

	for (uint32_t i = 0; i < image_count; i++) {
		ics->base.images[i].handle = remote_handles[i];
		ics->base.images[i].size = size;
		ics->base.images[i].use_dedicated_allocation = use_dedicated_allocation;
	}

	*out_xsc = &ics->base.base;

	return XRT_SUCCESS;
}

static xrt_result_t
swapchain_server_import(struct ipc_client_compositor *icc,
                        const struct xrt_swapchain_create_info *info,
                        struct xrt_image_native *native_images,
                        uint32_t image_count,
                        struct xrt_swapchain **out_xsc)
{
	struct ipc_arg_swapchain_from_native args = {0};
	xrt_graphics_buffer_handle_t handles[IPC_MAX_SWAPCHAIN_HANDLES] = {0};
	xrt_result_t r = XRT_SUCCESS;
	uint32_t id = 0;

	for (uint32_t i = 0; i < image_count; i++) {
		handles[i] = native_images[i].handle;
		args.sizes[i] = native_images[i].size;
	}

	// This does not consume the handles, it copies them.
	r = ipc_call_swapchain_import(icc->ipc_c,  // connection
	                              info,        // in
	                              &args,       // in
	                              handles,     // handles
	                              image_count, // handles
	                              &id);        // out
	if (r != XRT_SUCCESS) {
		return r;
	}

	struct ipc_client_swapchain *ics = U_TYPED_CALLOC(struct ipc_client_swapchain);
	ics->base.base.image_count = image_count;
	ics->base.base.wait_image = ipc_compositor_swapchain_wait_image;
	ics->base.base.acquire_image = ipc_compositor_swapchain_acquire_image;
	ics->base.base.release_image = ipc_compositor_swapchain_release_image;
	ics->base.base.destroy = ipc_compositor_swapchain_destroy;
	ics->base.base.reference.count = 1;
	ics->icc = icc;
	ics->id = id;

	// The handles were copied in the IPC call so we can reuse them here.
	for (uint32_t i = 0; i < image_count; i++) {
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
	struct xrt_swapchain_create_properties xsccp = {0};
	struct xrt_image_native *images = NULL;
	xrt_result_t xret;

	// Get any needed properties.
	xret = ipc_compositor_get_swapchain_create_properties(&icc->base.base, info, &xsccp);
	if (xret != XRT_SUCCESS) {
		// IPC error already reported.
		return xret;
	}

	// Alloc the array of structs for the images.
	images = U_TYPED_ARRAY_CALLOC(struct xrt_image_native, xsccp.image_count);

	// Now allocate the images themselves
	xret = xrt_images_allocate(xina, info, xsccp.image_count, images);
	if (xret != XRT_SUCCESS) {
		goto out_free;
	}

	/*
	 * The import function takes ownership of the handles,
	 * we do not need free them if the call succeeds.
	 */
	xret = swapchain_server_import(icc, info, images, xsccp.image_count, out_xsc);
	if (xret != XRT_SUCCESS) {
		xrt_images_free(xina, xsccp.image_count, images);
	}

out_free:
	free(images);

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
                                uint32_t image_count,
                                struct xrt_swapchain **out_xsc)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);
	return swapchain_server_import(icc, info, native_images, image_count, out_xsc);
}

static xrt_result_t
ipc_compositor_semaphore_create(struct xrt_compositor *xc,
                                xrt_graphics_sync_handle_t *out_handle,
                                struct xrt_compositor_semaphore **out_xcsem)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);
	uint32_t id = 0;
	xrt_graphics_sync_handle_t handle = XRT_GRAPHICS_SYNC_HANDLE_INVALID;

	IPC_CALL_CHK(ipc_call_compositor_semaphore_create(icc->ipc_c, &id, &handle, 1));
	if (res != XRT_SUCCESS) {
		return res;
	}

	struct ipc_client_compositor_semaphore *iccs = U_TYPED_CALLOC(struct ipc_client_compositor_semaphore);
	iccs->base.reference.count = 1;
	iccs->base.wait = ipc_client_compositor_semaphore_wait;
	iccs->base.destroy = ipc_client_compositor_semaphore_destroy;
	iccs->id = id;
	iccs->icc = icc;

	*out_handle = handle;
	*out_xcsem = &iccs->base;

	return XRT_SUCCESS;
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
	IPC_TRACE_MARKER();

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
	IPC_TRACE_MARKER();
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	uint64_t wake_up_time_ns = 0;

	IPC_CALL_CHK(ipc_call_compositor_predict_frame(icc->ipc_c,                     // Connection
	                                               out_frame_id,                   // Frame id
	                                               &wake_up_time_ns,               // When we should wake up
	                                               out_predicted_display_time,     // Display time
	                                               out_predicted_display_period)); // Current period

	// Wait until the given wake up time.
	u_wait_until(&icc->sleeper, wake_up_time_ns);

	// Signal that we woke up.
	res = ipc_call_compositor_wait_woke(icc->ipc_c, *out_frame_id);

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
ipc_compositor_layer_begin(struct xrt_compositor *xc,
                           int64_t frame_id,
                           uint64_t display_time_ns,
                           enum xrt_blend_mode env_blend_mode)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];

	slot->display_time_ns = display_time_ns;
	slot->env_blend_mode = env_blend_mode;

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
	struct ipc_layer_entry *layer = &slot->layers[icc->layers.layer_count];
	struct ipc_client_swapchain *l = ipc_client_swapchain(l_xsc);
	struct ipc_client_swapchain *r = ipc_client_swapchain(r_xsc);

	layer->xdev_id = 0; //! @todo Real id.
	layer->swapchain_ids[0] = l->id;
	layer->swapchain_ids[1] = r->id;
	layer->swapchain_ids[2] = -1;
	layer->swapchain_ids[3] = -1;
	layer->data = *data;

	// Increment the number of layers.
	icc->layers.layer_count++;

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
	struct ipc_layer_entry *layer = &slot->layers[icc->layers.layer_count];
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
	icc->layers.layer_count++;

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
	struct ipc_layer_entry *layer = &slot->layers[icc->layers.layer_count];
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);

	layer->xdev_id = 0; //! @todo Real id.
	layer->swapchain_ids[0] = ics->id;
	layer->swapchain_ids[1] = -1;
	layer->swapchain_ids[2] = -1;
	layer->swapchain_ids[3] = -1;
	layer->data = *data;

	// Increment the number of layers.
	icc->layers.layer_count++;

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
	slot->layer_count = icc->layers.layer_count;

	IPC_CALL_CHK(ipc_call_compositor_layer_sync( //
	    icc->ipc_c,                              //
	    frame_id,                                //
	    icc->layers.slot_id,                     //
	    &sync_handle,                            //
	    valid_sync ? 1 : 0,                      //
	    &icc->layers.slot_id));                  //

	// Reset.
	icc->layers.layer_count = 0;

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
ipc_compositor_layer_commit_with_semaphore(struct xrt_compositor *xc,
                                           int64_t frame_id,
                                           struct xrt_compositor_semaphore *xcsem,
                                           uint64_t value)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);
	struct ipc_client_compositor_semaphore *iccs = ipc_client_compositor_semaphore(xcsem);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];

	// Last bit of data to put in the shared memory area.
	slot->layer_count = icc->layers.layer_count;

	IPC_CALL_CHK(ipc_call_compositor_layer_sync_with_semaphore( //
	    icc->ipc_c,                                             //
	    frame_id,                                               //
	    icc->layers.slot_id,                                    //
	    iccs->id,                                               //
	    value,                                                  //
	    &icc->layers.slot_id));                                 //

	// Reset.
	icc->layers.layer_count = 0;

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

	IPC_CALL_CHK(ipc_call_session_destroy(icc->ipc_c));

	os_precise_sleeper_deinit(&icc->sleeper);

	icc->compositor_created = false;
}

static void
ipc_compositor_init(struct ipc_client_compositor *icc, struct xrt_compositor_native **out_xcn)
{
	icc->base.base.get_swapchain_create_properties = ipc_compositor_get_swapchain_create_properties;
	icc->base.base.create_swapchain = ipc_compositor_swapchain_create;
	icc->base.base.import_swapchain = ipc_compositor_swapchain_import;
	icc->base.base.create_semaphore = ipc_compositor_semaphore_create;
	icc->base.base.begin_session = ipc_compositor_begin_session;
	icc->base.base.end_session = ipc_compositor_end_session;
	icc->base.base.wait_frame = ipc_compositor_wait_frame;
	icc->base.base.begin_frame = ipc_compositor_begin_frame;
	icc->base.base.discard_frame = ipc_compositor_discard_frame;
	icc->base.base.layer_begin = ipc_compositor_layer_begin;
	icc->base.base.layer_stereo_projection = ipc_compositor_layer_stereo_projection;
	icc->base.base.layer_stereo_projection_depth = ipc_compositor_layer_stereo_projection_depth;
	icc->base.base.layer_quad = ipc_compositor_layer_quad;
	icc->base.base.layer_cube = ipc_compositor_layer_cube;
	icc->base.base.layer_cylinder = ipc_compositor_layer_cylinder;
	icc->base.base.layer_equirect1 = ipc_compositor_layer_equirect1;
	icc->base.base.layer_equirect2 = ipc_compositor_layer_equirect2;
	icc->base.base.layer_commit = ipc_compositor_layer_commit;
	icc->base.base.layer_commit_with_semaphore = ipc_compositor_layer_commit_with_semaphore;
	icc->base.base.destroy = ipc_compositor_destroy;
	icc->base.base.poll_events = ipc_compositor_poll_events;

	// Using in wait frame.
	os_precise_sleeper_init(&icc->sleeper);

	// Fetch info from the compositor, among it the format format list.
	get_info(&(icc->base.base), &icc->base.base.info);

	*out_xcn = &icc->base;
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
                               size_t in_image_count,
                               struct xrt_image_native *out_images)
{
	struct ipc_client_compositor *icc = container_of(xina, struct ipc_client_compositor, loopback_xina);

	int remote_fds[IPC_MAX_SWAPCHAIN_FDS] = {0};
	xrt_result_t r = XRT_SUCCESS;
	uint32_t image_count;
	uint32_t handle;
	uint64_t size;

	for (size_t i = 0; i < ARRAY_SIZE(remote_fds); i++) {
		remote_fds[i] = -1;
	}

	for (size_t i = 0; i < in_image_count; i++) {
		out_images[i].fd = -1;
		out_images[i].size = 0;
	}

	r = ipc_call_swapchain_create(icc->ipc_c,             // connection
	                              xsci,                   // in
	                              &handle,                // out
	                              &image_count,           // out
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
	if (image_count < in_image_count) {
		for (uint32_t k = 0; k < image_count && k < in_image_count; k++) {
			/*
			 * Overly-broad condition: we know that any fd not touched by
			 * ipc_call_swapchain_create will be -1.
			 */
			if (remote_fds[k] >= 0) {
				close(remote_fds[k]);
				remote_fds[k] = -1;
			}
		}

		return XRT_ERROR_IPC_FAILURE;
	}

	// Copy up to in_image_count, or image_count what ever is lowest.
	uint32_t i = 0;
	for (; i < image_count && i < in_image_count; i++) {
		out_images[i].fd = remote_fds[i];
		out_images[i].size = size;
	}

	// Close any fds we are not interested in.
	for (; i < image_count; i++) {
		/*
		 * Overly-broad condition: we know that any fd not touched by
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
                           size_t image_count,
                           struct xrt_image_native *out_images)
{
	for (uint32_t i = 0; i < image_count; i++) {
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

	// Needs to be done before init.
	IPC_CALL_CHK(ipc_call_session_create(icc->ipc_c, xsi));

	if (res != XRT_SUCCESS) {
		return res;
	}

	// Needs to be done after session create call.
	ipc_compositor_init(icc, out_xcn);

	icc->compositor_created = true;

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

/*!
 *
 *
 * This actually creates an IPC client "native" compositor with deferred initialization.
 * It owns a special implementation of the @ref xrt_system_compositor interface
 * whose "create_native_compositor" method actually completes the deferred initialization
 * of the compositor, effectively finishing creation of a compositor IPC proxy.
 */
int
ipc_client_create_system_compositor(struct ipc_connection *ipc_c,
                                    struct xrt_image_native_allocator *xina,
                                    struct xrt_device *xdev,
                                    struct xrt_system_compositor **out_xcs)
{
	struct ipc_client_compositor *c = U_TYPED_CALLOC(struct ipc_client_compositor);

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

	// Fetch info from the system compositor.
	get_system_info(c, &c->system.info);

	*out_xcs = &c->system;

	return 0;
}
