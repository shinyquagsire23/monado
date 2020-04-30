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

#include "util/u_misc.h"

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

#define CALL_CHK(call)                                                         \
	if ((call) != IPC_SUCCESS) {                                           \
		IPC_DEBUG(icc->ipc_c, "IPC: %s call error!", __func__);        \
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

	CALL_CHK(ipc_call_swapchain_destroy(icc->ipc_c, ics->id));

	free(xsc);
}

static bool
ipc_compositor_swapchain_wait_image(struct xrt_swapchain *xsc,
                                    uint64_t timeout,
                                    uint32_t index)
{
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);
	struct ipc_client_compositor *icc = ics->icc;

	CALL_CHK(
	    ipc_call_swapchain_wait_image(icc->ipc_c, ics->id, timeout, index));

	return true;
}

static bool
ipc_compositor_swapchain_acquire_image(struct xrt_swapchain *xsc,
                                       uint32_t *out_index)
{
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);
	struct ipc_client_compositor *icc = ics->icc;

	CALL_CHK(
	    ipc_call_swapchain_acquire_image(icc->ipc_c, ics->id, out_index));

	return true;
}

static bool
ipc_compositor_swapchain_release_image(struct xrt_swapchain *xsc,
                                       uint32_t index)
{
	struct ipc_client_swapchain *ics = ipc_client_swapchain(xsc);
	struct ipc_client_compositor *icc = ics->icc;

	CALL_CHK(ipc_call_swapchain_release_image(icc->ipc_c, ics->id, index));

	return true;
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
	ipc_result_t r = 0;
	uint32_t handle;
	uint32_t num_images;
	uint64_t size;

	r = ipc_call_swapchain_create(icc->ipc_c,             // connection
	                              width,                  // in
	                              height,                 // in
	                              format,                 // in
	                              &handle,                // out
	                              &num_images,            // out
	                              &size,                  // out
	                              remote_fds,             // fds
	                              IPC_MAX_SWAPCHAIN_FDS); // fds
	if (r != IPC_SUCCESS) {
		return NULL;
	}

	struct ipc_client_swapchain *ics =
	    U_TYPED_CALLOC(struct ipc_client_swapchain);
	ics->base.base.array_size = 1;
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

static void
ipc_compositor_begin_session(struct xrt_compositor *xc,
                             enum xrt_view_type view_type)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_SPEW(icc->ipc_c, "IPC: compositor begin session");

	CALL_CHK(ipc_call_session_begin(icc->ipc_c));
}

static void
ipc_compositor_end_session(struct xrt_compositor *xc)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_SPEW(icc->ipc_c, "IPC: compositor end session");

	CALL_CHK(ipc_call_session_end(icc->ipc_c));
}

static void
ipc_compositor_get_formats(struct xrt_compositor *xc,
                           uint32_t *num_formats,
                           int64_t *formats)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	IPC_SPEW(icc->ipc_c, "IPC: compositor get_formats");

	struct ipc_formats_info info;
	CALL_CHK(ipc_call_compositor_get_formats(icc->ipc_c, &info));

	*num_formats = info.num_formats;
	memcpy(formats, info.formats, sizeof(int64_t) * (*num_formats));
}

static void
ipc_compositor_wait_frame(struct xrt_compositor *xc,
                          uint64_t *predicted_display_time,
                          uint64_t *predicted_display_period)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	CALL_CHK(ipc_call_compositor_wait_frame(
	    icc->ipc_c, predicted_display_period, predicted_display_time));
}

static void
ipc_compositor_begin_frame(struct xrt_compositor *xc)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	CALL_CHK(ipc_call_compositor_begin_frame(icc->ipc_c));
}

#if 0 /* LAYERS */
static void
ipc_compositor_layer_begin(struct xrt_compositor *xc,
                           enum xrt_blend_mode env_blend_mode)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	icc->layers.env_blend_mode = env_blend_mode;
}

static void
ipc_compositor_layer_stereo_projection(
    struct xrt_compositor *xc,
    uint64_t timestamp,
    struct xrt_device *xdev,
    enum xrt_input_name name,
    enum xrt_layer_composition_flags layer_flags,
    struct xrt_swapchain *l_sc,
    uint32_t l_image_index,
    struct xrt_rect *l_rect,
    uint32_t l_array_index,
    struct xrt_fov *l_fov,
    struct xrt_pose *l_pose,
    struct xrt_swapchain *r_sc,
    uint32_t r_image_index,
    struct xrt_rect *r_rect,
    uint32_t r_array_index,
    struct xrt_fov *r_fov,
    struct xrt_pose *r_pose)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];
	struct ipc_layer_entry *layer = &slot->layers[icc->layers.num_layers];
	struct ipc_layer_stereo_projection *stereo = &layer->stereo;
	struct ipc_client_swapchain *l = ipc_client_swapchain(l_sc);
	struct ipc_client_swapchain *r = ipc_client_swapchain(r_sc);

	stereo->timestamp = timestamp;
	stereo->xdev_id = 0; //! @todo Real id.
	stereo->name = name;
	stereo->layer_flags = layer_flags;
	stereo->l.swapchain_id = l->id;
	stereo->l.image_index = l_image_index;
	stereo->l.rect = *l_rect;
	stereo->l.array_index = l_array_index;
	stereo->l.fov = *l_fov;
	stereo->l.pose = *l_pose;
	stereo->r.swapchain_id = r->id;
	stereo->r.image_index = r_image_index;
	stereo->r.rect = *r_rect;
	stereo->r.array_index = r_array_index;
	stereo->r.fov = *r_fov;
	stereo->r.pose = *r_pose;

	// Increment the number of layers.
	icc->layers.num_layers++;
}

static void
ipc_compositor_layer_quad(struct xrt_compositor *xc,
                          uint64_t timestamp,
                          struct xrt_device *xdev,
                          enum xrt_input_name name,
                          enum xrt_layer_composition_flags layer_flags,
                          enum xrt_layer_eye_visibility visibility,
                          struct xrt_swapchain *sc,
                          uint32_t image_index,
                          struct xrt_rect *rect,
                          uint32_t array_index,
                          struct xrt_pose *pose,
                          struct xrt_vec2 *size)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];
	struct ipc_layer_entry *layer = &slot->layers[icc->layers.num_layers];
	struct ipc_layer_quad *quad = &layer->quad;
	struct ipc_client_swapchain *ics = ipc_client_swapchain(sc);

	quad->timestamp = timestamp;
	quad->xdev_id = 0; //! @todo Real id.
	quad->name = name;
	quad->layer_flags = layer_flags;
	quad->swapchain_id = ics->id;
	quad->image_index = image_index;
	quad->rect = *rect;
	quad->array_index = array_index;
	quad->pose = *pose;
	quad->size = *size;

	// Increment the number of layers.
	icc->layers.num_layers++;
}

static void
ipc_compositor_layer_commit(struct xrt_compositor *xc)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];

	// Last bit of data to put in the shared memory area.
	slot->num_layers = icc->layers.num_layers;

	CALL_CHK(ipc_call_compositor_layer_sync(icc->ipc_c, icc->layers.slot_id,
	                                        &icc->layers.slot_id));

	// Reset.
	icc->layers.num_layers = 0;
}

#else

static void
ipc_compositor_end_frame(struct xrt_compositor *xc,
                         enum xrt_blend_mode blend_mode,
                         struct xrt_swapchain **xscs,
                         const uint32_t *image_index,
                         uint32_t *layers,
                         uint32_t num_swapchains)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	struct ipc_shared_memory *ism = icc->ipc_c->ism;
	struct ipc_layer_slot *slot = &ism->slots[icc->layers.slot_id];
	struct ipc_layer_entry *layer = &slot->layers[icc->layers.num_layers];
	struct ipc_layer_stereo_projection *stereo = &layer->stereo;
	struct ipc_client_swapchain *l = ipc_client_swapchain(xscs[0]);
	struct ipc_client_swapchain *r = ipc_client_swapchain(xscs[1]);

	// stereo->timestamp = timestamp;
	// stereo->xdev_id = 0; //! @todo Real id.
	// stereo->name = name;
	// stereo->layer_flags = layer_flags;
	stereo->l.swapchain_id = l->id;
	stereo->l.image_index = image_index[0];
	// stereo->l.rect = *l_rect;
	stereo->l.array_index = layers[0];
	// stereo->l.fov = *l_fov;
	// stereo->l.pose = *l_pose;
	stereo->r.swapchain_id = r->id;
	stereo->r.image_index = image_index[1];
	// stereo->r.rect = *r_rect;
	stereo->r.array_index = layers[1];
	// stereo->r.fov = *r_fov;
	// stereo->r.pose = *r_pose;

	// Last bit of data to put in the shared memory area.
	slot->num_layers = icc->layers.num_layers;

	CALL_CHK(ipc_call_compositor_layer_sync(icc->ipc_c, icc->layers.slot_id,
	                                        &icc->layers.slot_id));

	// Reset.
	icc->layers.num_layers = 0;
}
#endif

static void
ipc_compositor_discard_frame(struct xrt_compositor *xc)
{
	struct ipc_client_compositor *icc = ipc_client_compositor(xc);

	CALL_CHK(ipc_call_compositor_discard_frame(icc->ipc_c));
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
#if 0 /* LAYERS */
	c->base.base.layer_begin = ipc_compositor_layer_begin;
	c->base.base.layer_stereo_projection =
	    ipc_compositor_layer_stereo_projection;
	c->base.base.layer_quad = ipc_compositor_layer_quad;
	c->base.base.layer_commit = ipc_compositor_layer_commit;
#else
	c->base.base.end_frame = ipc_compositor_end_frame;
#endif
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
