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

#include "ipc_protocol.h"
#include "ipc_client.h"
#include "ipc_client_generated.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <assert.h>


/*
 *
 * Internal structs and helpers.
 *
 */
/*!
 * Client proxy for an xrt_compositor_fd implementation over IPC.
 * @implements xrt_compositor_fd
 */
struct ipc_client_compositor
{
	struct xrt_compositor_fd base;

	ipc_connection_t *ipc_c;

	struct
	{
		//! Id that we are currently using for submitting layers.
		uint32_t slot_id;

		uint32_t num_layers;

		enum xrt_blend_mode env_blend_mode;
	} layers;
};

/*!
 * Client proxy for an xrt_swapchain_fd implementation over IPC.
 * @implements xrt_swapchain_fd
 */
struct ipc_client_swapchain
{
	struct xrt_swapchain_fd base;

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
compositor_disconnect(ipc_connection_t *ipc_c)
{
	if (ipc_c->socket_fd < 0) {
		return;
	}

	close(ipc_c->socket_fd);
	ipc_c->socket_fd = -1;
}

#define IPC_CALL_CHK(call)                                                     \
	xrt_result_t res = (call);                                             \
	if (res == XRT_ERROR_IPC_FAILURE) {                                    \
		IPC_ERROR(icc->ipc_c, "IPC: %s call error!", __func__);        \
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
ipc_compositor_swapchain_wait_image(struct xrt_swapchain *xsc,
                                    uint64_t timeout,
                                    uint32_t index)
{
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);
	struct ipc_client_compositor *icc = ics->icc;

	IPC_CALL_CHK(
	    ipc_call_swapchain_wait_image(icc->ipc_c, ics->id, timeout, index));

	return res;
}

static xrt_result_t
ipc_compositor_swapchain_acquire_image(struct xrt_swapchain *xsc,
                                       uint32_t *out_index)
{
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);
	struct ipc_client_compositor *icc = ics->icc;

	IPC_CALL_CHK(
	    ipc_call_swapchain_acquire_image(icc->ipc_c, ics->id, out_index));

	return res;
}

static xrt_result_t
ipc_compositor_swapchain_release_image(struct xrt_swapchain *xsc,
                                       uint32_t index)
{
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);
	struct ipc_client_compositor *icc = ics->icc;

	IPC_CALL_CHK(
	    ipc_call_swapchain_release_image(icc->ipc_c, ics->id, index));

	return res;
}


/*
 *
 * Compositor functions.
 *
 */

static struct xrt_swapchain *
ipc_compositor_swapchain_create(struct xrt_compositor *xc,
                                enum xrt_swapchain_create_flags create,
                                enum xrt_swapchain_usage_bits bits,
                                int64_t format,
                                uint32_t sample_count,
                                uint32_t width,
                                uint32_t height,
                                uint32_t face_count,
                                uint32_t array_size,
                                uint32_t mip_count)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	int remote_fds[IPC_MAX_SWAPCHAIN_FDS] = {0};
	xrt_result_t r = XRT_SUCCESS;
	uint32_t handle;
	uint32_t num_images;
	uint64_t size;

	r = ipc_call_swapchain_create(icc->ipc_c,             // connection
	                              create,                 // in
	                              bits,                   // in
	                              format,                 // in
	                              sample_count,           // in
	                              width,                  // in
	                              height,                 // in
	                              face_count,             // in
	                              array_size,             // in
	                              mip_count,              // in
	                              &handle,                // out
	                              &num_images,            // out
	                              &size,                  // out
	                              remote_fds,             // fds
	                              IPC_MAX_SWAPCHAIN_FDS); // fds
	if (r != XRT_SUCCESS) {
		return NULL;
	}

	struct ipc_client_swapchain *ics =
	    U_TYPED_CALLOC(struct ipc_client_swapchain);
	ics->base.base.num_images = num_images;
	ics->base.base.wait_image = ipc_compositor_swapchain_wait_image;
	ics->base.base.acquire_image = ipc_compositor_swapchain_acquire_image;
	ics->base.base.release_image = ipc_compositor_swapchain_release_image;
	ics->base.base.destroy = ipc_compositor_swapchain_destroy;
	ics->icc = icc;
	ics->id = handle;

	for (uint32_t i = 0; i < num_images; i++) {
		ics->base.images[i].fd = remote_fds[i];
		ics->base.images[i].size = size;
	}

	return &ics->base.base;
}

static xrt_result_t
ipc_compositor_begin_session(struct xrt_compositor *xc,
                             enum xrt_view_type view_type)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_SPEW(icc->ipc_c, "IPC: compositor begin session");

	IPC_CALL_CHK(ipc_call_session_begin(icc->ipc_c));

	return res;
}

static xrt_result_t
ipc_compositor_end_session(struct xrt_compositor *xc)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_SPEW(icc->ipc_c, "IPC: compositor end session");

	IPC_CALL_CHK(ipc_call_session_end(icc->ipc_c));

	return res;
}

static xrt_result_t
ipc_compositor_get_formats(struct xrt_compositor *xc,
                           uint32_t *num_formats,
                           int64_t *formats)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_SPEW(icc->ipc_c, "IPC: compositor get_formats");

	struct ipc_formats_info info;
	IPC_CALL_CHK(ipc_call_compositor_get_formats(icc->ipc_c, &info));

	*num_formats = info.num_formats;
	memcpy(formats, info.formats, sizeof(int64_t) * (*num_formats));

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

	IPC_CALL_CHK(ipc_call_compositor_wait_frame(
	    icc->ipc_c,                   // Connection
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

	fprintf(stderr, "%s: Slept %i.%02ims\n", __func__, (int)ms100 / 100,
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
ipc_compositor_layer_begin(struct xrt_compositor *xc,
                           int64_t frame_id,
                           enum xrt_blend_mode env_blend_mode)
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
                                       struct xrt_layer_data *data)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];
	struct ipc_layer_entry *layer = &slot->layers[icc->layers.num_layers];
	struct ipc_client_swapchain *l = ipc_client_swapchain(l_xsc);
	struct ipc_client_swapchain *r = ipc_client_swapchain(r_xsc);

	layer->xdev_id = 0; //! @todo Real id.
	layer->swapchain_ids[0] = l->id;
	layer->swapchain_ids[1] = r->id;
	layer->data = *data;

	// Increment the number of layers.
	icc->layers.num_layers++;

	return XRT_SUCCESS;
}

static xrt_result_t
ipc_compositor_layer_quad(struct xrt_compositor *xc,
                          struct xrt_device *xdev,
                          struct xrt_swapchain *xsc,
                          struct xrt_layer_data *data)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];
	struct ipc_layer_entry *layer = &slot->layers[icc->layers.num_layers];
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);

	assert(data->type == XRT_LAYER_QUAD);

	layer->xdev_id = 0; //! @todo Real id.
	layer->swapchain_ids[0] = ics->id;
	layer->swapchain_ids[1] = -1;
	layer->data = *data;

	// Increment the number of layers.
	icc->layers.num_layers++;

	return XRT_SUCCESS;
}

static xrt_result_t
ipc_compositor_layer_commit(struct xrt_compositor *xc, int64_t frame_id)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];

	// Last bit of data to put in the shared memory area.
	slot->num_layers = icc->layers.num_layers;

	IPC_CALL_CHK(ipc_call_compositor_layer_sync(
	    icc->ipc_c, frame_id, icc->layers.slot_id, &icc->layers.slot_id));

	// Reset.
	icc->layers.num_layers = 0;

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

	IPC_SPEW(icc->ipc_c, "IPC:  NOT IMPLEMENTED compositor destroy");
}


/*
 *
 * 'Exported' functions.
 *
 */

int
ipc_client_compositor_create(ipc_connection_t *ipc_c,
                             struct xrt_device *xdev,
                             bool flip_y,
                             struct xrt_compositor_fd **out_xcfd)
{
	struct ipc_client_compositor *c =
	    U_TYPED_CALLOC(struct ipc_client_compositor);

	c->base.base.create_swapchain = ipc_compositor_swapchain_create;
	c->base.base.begin_session = ipc_compositor_begin_session;
	c->base.base.end_session = ipc_compositor_end_session;
	c->base.base.wait_frame = ipc_compositor_wait_frame;
	c->base.base.begin_frame = ipc_compositor_begin_frame;
	c->base.base.discard_frame = ipc_compositor_discard_frame;
	c->base.base.layer_begin = ipc_compositor_layer_begin;
	c->base.base.layer_stereo_projection =
	    ipc_compositor_layer_stereo_projection;
	c->base.base.layer_quad = ipc_compositor_layer_quad;
	c->base.base.layer_commit = ipc_compositor_layer_commit;
	c->base.base.destroy = ipc_compositor_destroy;
	c->ipc_c = ipc_c;

	// fetch our format list on client compositor construction
	int64_t formats[IPC_MAX_FORMATS] = {0};
	uint32_t num_formats = 0;
	ipc_compositor_get_formats(&(c->base.base), &num_formats, formats);
	// TODO: client compositor format count is hardcoded
	c->base.base.num_formats = 0;
	for (uint32_t i = 0; i < 8; i++) {
		if (i < num_formats) {
			c->base.base.formats[i] = formats[i];
			c->base.base.num_formats++;
		}
	}

	*out_xcfd = &c->base;

	return 0;
}
