// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Multi client wrapper compositor.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_multi
 */

#include "xrt/xrt_gfx_native.h"

#include "os/os_time.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_handles.h"
#include "util/u_trace_marker.h"
#include "util/u_distortion_mesh.h"

#include "multi/comp_multi_private.h"

#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef XRT_GRAPHICS_SYNC_HANDLE_IS_FD
#include <unistd.h>
#endif


/*
 *
 * Slot management functions.
 *
 */

static void
slot_clear(struct multi_layer_slot *slot)
{
	for (size_t i = 0; i < slot->num_layers; i++) {
		for (size_t k = 0; k < ARRAY_SIZE(slot->layers[i].xscs); k++) {
			xrt_swapchain_reference(&slot->layers[i].xscs[k], NULL);
		}
	}

	U_ZERO(slot);
}

static void
slot_move_and_clear(struct multi_layer_slot *dst, struct multi_layer_slot *src)
{
	slot_clear(dst);

	// All references are kept.
	*dst = *src;

	U_ZERO(src);
}


/*
 *
 * Event management functions.
 *
 */

void
multi_compositor_push_event(struct multi_compositor *mc, const union xrt_compositor_event *xce)
{
	struct multi_event *me = U_TYPED_CALLOC(struct multi_event);
	me->xce = *xce;

	os_mutex_lock(&mc->event.mutex);

	// Find the last slot.
	struct multi_event **slot = &mc->event.next;
	while (*slot != NULL) {
		slot = &(*slot)->next;
	}

	*slot = me;

	os_mutex_unlock(&mc->event.mutex);
}

static void
pop_event(struct multi_compositor *mc, union xrt_compositor_event *out_xce)
{
	out_xce->type = XRT_COMPOSITOR_EVENT_NONE;

	os_mutex_lock(&mc->event.mutex);

	if (mc->event.next != NULL) {
		struct multi_event *me = mc->event.next;

		*out_xce = me->xce;
		mc->event.next = me->next;
		free(me);
	}

	os_mutex_unlock(&mc->event.mutex);
}

static void
drain_events(struct multi_compositor *mc)
{
	union xrt_compositor_event xce;
	do {
		pop_event(mc, &xce);
	} while (xce.type != XRT_COMPOSITOR_EVENT_NONE);
}


/*
 *
 * Compositor functions.
 *
 */

static xrt_result_t
multi_compositor_create_swapchain(struct xrt_compositor *xc,
                                  const struct xrt_swapchain_create_info *info,
                                  struct xrt_swapchain **out_xsc)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);

	return xrt_comp_create_swapchain(&mc->msc->xcn->base, info, out_xsc);
}

static xrt_result_t
multi_compositor_import_swapchain(struct xrt_compositor *xc,
                                  const struct xrt_swapchain_create_info *info,
                                  struct xrt_image_native *native_images,
                                  uint32_t num_images,
                                  struct xrt_swapchain **out_xsc)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);

	return xrt_comp_import_swapchain(&mc->msc->xcn->base, info, native_images, num_images, out_xsc);
}

static xrt_result_t
multi_compositor_import_fence(struct xrt_compositor *xc,
                              xrt_graphics_sync_handle_t handle,
                              struct xrt_compositor_fence **out_xcf)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);

	return xrt_comp_import_fence(&mc->msc->xcn->base, handle, out_xcf);
}

static xrt_result_t
multi_compositor_begin_session(struct xrt_compositor *xc, enum xrt_view_type type)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);
	(void)mc;

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_end_session(struct xrt_compositor *xc)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);
	(void)mc;

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_predict_frame(struct xrt_compositor *xc,
                               int64_t *out_frame_id,
                               uint64_t *out_wake_time_ns,
                               uint64_t *out_predicted_gpu_time_ns,
                               uint64_t *out_predicted_display_time_ns,
                               uint64_t *out_predicted_display_period_ns)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);

	os_mutex_lock(&mc->msc->list_and_timing_lock);

	u_rt_predict(                         //
	    mc->urt,                          //
	    out_frame_id,                     //
	    out_wake_time_ns,                 //
	    out_predicted_display_time_ns,    //
	    out_predicted_display_period_ns); //

	os_mutex_unlock(&mc->msc->list_and_timing_lock);

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_mark_frame(struct xrt_compositor *xc,
                            int64_t frame_id,
                            enum xrt_compositor_frame_point point,
                            uint64_t when_ns)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);

	switch (point) {
	case XRT_COMPOSITOR_FRAME_POINT_WOKE:
		os_mutex_lock(&mc->msc->list_and_timing_lock);
		uint64_t now_ns = os_monotonic_get_ns();
		u_rt_mark_point(mc->urt, frame_id, U_TIMING_POINT_WAKE_UP, now_ns);
		os_mutex_unlock(&mc->msc->list_and_timing_lock);
		break;
	default: assert(false);
	}

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_wait_frame(struct xrt_compositor *xc,
                            int64_t *out_frame_id,
                            uint64_t *out_predicted_display_time_ns,
                            uint64_t *out_predicted_display_period_ns)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);

	int64_t frame_id = -1;
	uint64_t wake_up_time_ns = 0;
	uint64_t predicted_gpu_time_ns = 0;

	xrt_comp_predict_frame(               //
	    xc,                               //
	    &frame_id,                        //
	    &wake_up_time_ns,                 //
	    &predicted_gpu_time_ns,           //
	    out_predicted_display_time_ns,    //
	    out_predicted_display_period_ns); //

	uint64_t now_ns = os_monotonic_get_ns();
	if (now_ns < wake_up_time_ns) {
		uint32_t delay = (uint32_t)(wake_up_time_ns - now_ns);
		os_precise_sleeper_nanosleep(&mc->sleeper, delay);
	}

	now_ns = os_monotonic_get_ns();

	xrt_comp_mark_frame(xc, frame_id, XRT_COMPOSITOR_FRAME_POINT_WOKE, now_ns);

	*out_frame_id = frame_id;

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_begin_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);

	os_mutex_lock(&mc->msc->list_and_timing_lock);
	uint64_t now_ns = os_monotonic_get_ns();
	u_rt_mark_point(mc->urt, frame_id, U_TIMING_POINT_BEGIN, now_ns);
	os_mutex_unlock(&mc->msc->list_and_timing_lock);

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_discard_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);

	os_mutex_lock(&mc->msc->list_and_timing_lock);
	u_rt_mark_discarded(mc->urt, frame_id);
	os_mutex_unlock(&mc->msc->list_and_timing_lock);

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_layer_begin(struct xrt_compositor *xc,
                             int64_t frame_id,
                             uint64_t display_time_ns,
                             enum xrt_blend_mode env_blend_mode)
{
	struct multi_compositor *mc = multi_compositor(xc);

	assert(mc->progress.num_layers == 0);
	U_ZERO(&mc->progress);

	mc->progress.active = true;
	mc->progress.display_time_ns = display_time_ns;
	mc->progress.env_blend_mode = env_blend_mode;

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_layer_stereo_projection(struct xrt_compositor *xc,
                                         struct xrt_device *xdev,
                                         struct xrt_swapchain *l_xsc,
                                         struct xrt_swapchain *r_xsc,
                                         const struct xrt_layer_data *data)
{
	struct multi_compositor *mc = multi_compositor(xc);
	(void)mc;

	size_t index = mc->progress.num_layers++;
	mc->progress.layers[index].xdev = xdev;
	xrt_swapchain_reference(&mc->progress.layers[index].xscs[0], l_xsc);
	xrt_swapchain_reference(&mc->progress.layers[index].xscs[1], r_xsc);
	mc->progress.layers[index].data = *data;

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_layer_stereo_projection_depth(struct xrt_compositor *xc,
                                               struct xrt_device *xdev,
                                               struct xrt_swapchain *l_xsc,
                                               struct xrt_swapchain *r_xsc,
                                               struct xrt_swapchain *l_d_xsc,
                                               struct xrt_swapchain *r_d_xsc,
                                               const struct xrt_layer_data *data)
{
	struct multi_compositor *mc = multi_compositor(xc);

	size_t index = mc->progress.num_layers++;
	mc->progress.layers[index].xdev = xdev;
	xrt_swapchain_reference(&mc->progress.layers[index].xscs[0], l_xsc);
	xrt_swapchain_reference(&mc->progress.layers[index].xscs[1], r_xsc);
	xrt_swapchain_reference(&mc->progress.layers[index].xscs[2], l_d_xsc);
	xrt_swapchain_reference(&mc->progress.layers[index].xscs[3], r_d_xsc);
	mc->progress.layers[index].data = *data;

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_layer_quad(struct xrt_compositor *xc,
                            struct xrt_device *xdev,
                            struct xrt_swapchain *xsc,
                            const struct xrt_layer_data *data)
{
	struct multi_compositor *mc = multi_compositor(xc);

	size_t index = mc->progress.num_layers++;
	mc->progress.layers[index].xdev = xdev;
	xrt_swapchain_reference(&mc->progress.layers[index].xscs[0], xsc);
	mc->progress.layers[index].data = *data;

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_layer_cube(struct xrt_compositor *xc,
                            struct xrt_device *xdev,
                            struct xrt_swapchain *xsc,
                            const struct xrt_layer_data *data)
{
	struct multi_compositor *mc = multi_compositor(xc);

	size_t index = mc->progress.num_layers++;
	mc->progress.layers[index].xdev = xdev;
	xrt_swapchain_reference(&mc->progress.layers[index].xscs[0], xsc);
	mc->progress.layers[index].data = *data;

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_layer_cylinder(struct xrt_compositor *xc,
                                struct xrt_device *xdev,
                                struct xrt_swapchain *xsc,
                                const struct xrt_layer_data *data)
{
	struct multi_compositor *mc = multi_compositor(xc);

	size_t index = mc->progress.num_layers++;
	mc->progress.layers[index].xdev = xdev;
	xrt_swapchain_reference(&mc->progress.layers[index].xscs[0], xsc);
	mc->progress.layers[index].data = *data;

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_layer_equirect1(struct xrt_compositor *xc,
                                 struct xrt_device *xdev,
                                 struct xrt_swapchain *xsc,
                                 const struct xrt_layer_data *data)
{
	struct multi_compositor *mc = multi_compositor(xc);

	size_t index = mc->progress.num_layers++;
	mc->progress.layers[index].xdev = xdev;
	xrt_swapchain_reference(&mc->progress.layers[index].xscs[0], xsc);
	mc->progress.layers[index].data = *data;

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_layer_equirect2(struct xrt_compositor *xc,
                                 struct xrt_device *xdev,
                                 struct xrt_swapchain *xsc,
                                 const struct xrt_layer_data *data)
{
	struct multi_compositor *mc = multi_compositor(xc);

	size_t index = mc->progress.num_layers++;
	mc->progress.layers[index].xdev = xdev;
	xrt_swapchain_reference(&mc->progress.layers[index].xscs[0], xsc);
	mc->progress.layers[index].data = *data;

	return XRT_SUCCESS;
}

static void
wait_fence(struct xrt_compositor_fence **xcf_ptr)
{
	COMP_TRACE_MARKER();
	xrt_compositor_fence_wait(*xcf_ptr, UINT64_MAX);
	xrt_compositor_fence_destroy(xcf_ptr);
}

static void
wait_for_scheduled_free(struct multi_compositor *mc)
{
	COMP_TRACE_MARKER();

	os_mutex_lock(&mc->slot_lock);

	// Block here if the scheduled slot is not clear.
	while (mc->scheduled.active) {

		// Replace the scheduled frame if it's in the past.
		uint64_t now_ns = os_monotonic_get_ns();
		if (mc->scheduled.display_time_ns < now_ns) {
			break;
		}

		os_mutex_unlock(&mc->slot_lock);

		os_nanosleep(U_TIME_1MS_IN_NS);

		os_mutex_lock(&mc->slot_lock);
	}

	slot_move_and_clear(&mc->scheduled, &mc->progress);

	os_mutex_unlock(&mc->slot_lock);
}

static xrt_result_t
multi_compositor_layer_commit(struct xrt_compositor *xc, int64_t frame_id, xrt_graphics_sync_handle_t sync_handle)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);
	struct xrt_compositor_fence *xcf = NULL;

	do {
		if (!xrt_graphics_sync_handle_is_valid(sync_handle)) {
			break;
		}

		xrt_result_t xret = xrt_comp_import_fence( //
		    &mc->msc->xcn->base,                   //
		    sync_handle,                           //
		    &xcf);                                 //
		/*!
		 * If import_fence succeeded, we have transferred ownership to
		 * the compositor no need to do anything more. If the call
		 * failed we need to close the handle.
		 */
		if (xret == XRT_SUCCESS) {
			break;
		}

		u_graphics_sync_unref(&sync_handle);
	} while (false); // Goto without the labels.

	if (xcf != NULL) {
		wait_fence(&xcf);
	}

	wait_for_scheduled_free(mc);

	os_mutex_lock(&mc->msc->list_and_timing_lock);
	u_rt_mark_delivered(mc->urt, frame_id);
	os_mutex_unlock(&mc->msc->list_and_timing_lock);

	return XRT_SUCCESS;
}

static xrt_result_t
multi_compositor_poll_events(struct xrt_compositor *xc, union xrt_compositor_event *out_xce)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);

	pop_event(mc, out_xce);

	return XRT_SUCCESS;
}

static void
multi_compositor_destroy(struct xrt_compositor *xc)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = multi_compositor(xc);

	os_mutex_lock(&mc->msc->list_and_timing_lock);

	// Remove it from the list of clients.
	for (size_t i = 0; i < MULTI_MAX_CLIENTS; i++) {
		if (mc->msc->clients[i] == mc) {
			mc->msc->clients[i] = NULL;
		}
	}

	os_mutex_unlock(&mc->msc->list_and_timing_lock);

	drain_events(mc);

	// We are now off the rendering list, clear slots for any swapchains.
	slot_clear(&mc->progress);
	slot_clear(&mc->scheduled);
	slot_clear(&mc->delivered);

	// Does null checking.
	u_rt_destroy(&mc->urt);

	os_precise_sleeper_deinit(&mc->sleeper);

	free(mc);
}

void
multi_compositor_deliver_any_frames(struct multi_compositor *mc, uint64_t display_time_ns)
{
	os_mutex_lock(&mc->slot_lock);

	if (!mc->scheduled.active) {
		os_mutex_unlock(&mc->slot_lock);
		return;
	}

	if (time_is_greater_then_or_within_half_ms(display_time_ns, mc->scheduled.display_time_ns)) {
		slot_move_and_clear(&mc->delivered, &mc->scheduled);
	}

	os_mutex_unlock(&mc->slot_lock);
}

xrt_result_t
multi_compositor_create(struct multi_system_compositor *msc,
                        const struct xrt_session_info *xsi,
                        struct xrt_compositor_native **out_xcn)
{
	COMP_TRACE_MARKER();

	struct multi_compositor *mc = U_TYPED_CALLOC(struct multi_compositor);

	mc->base.base.create_swapchain = multi_compositor_create_swapchain;
	mc->base.base.import_swapchain = multi_compositor_import_swapchain;
	mc->base.base.import_fence = multi_compositor_import_fence;
	mc->base.base.begin_session = multi_compositor_begin_session;
	mc->base.base.end_session = multi_compositor_end_session;
	mc->base.base.predict_frame = multi_compositor_predict_frame;
	mc->base.base.mark_frame = multi_compositor_mark_frame;
	mc->base.base.wait_frame = multi_compositor_wait_frame;
	mc->base.base.begin_frame = multi_compositor_begin_frame;
	mc->base.base.discard_frame = multi_compositor_discard_frame;
	mc->base.base.layer_begin = multi_compositor_layer_begin;
	mc->base.base.layer_stereo_projection = multi_compositor_layer_stereo_projection;
	mc->base.base.layer_stereo_projection_depth = multi_compositor_layer_stereo_projection_depth;
	mc->base.base.layer_quad = multi_compositor_layer_quad;
	mc->base.base.layer_cube = multi_compositor_layer_cube;
	mc->base.base.layer_cylinder = multi_compositor_layer_cylinder;
	mc->base.base.layer_equirect1 = multi_compositor_layer_equirect1;
	mc->base.base.layer_equirect2 = multi_compositor_layer_equirect2;
	mc->base.base.layer_commit = multi_compositor_layer_commit;
	mc->base.base.destroy = multi_compositor_destroy;
	mc->base.base.poll_events = multi_compositor_poll_events;
	mc->msc = msc;
	mc->xsi = *xsi;

	// Passthrough our formats from the native compositor to the client.
	mc->base.base.info = msc->xcn->base.info;

	// Using in wait frame.
	os_precise_sleeper_init(&mc->sleeper);

	// This is safe to do without a lock since we are not on the list yet.
	u_rt_create(&mc->urt);

	os_mutex_lock(&msc->list_and_timing_lock);

	// Meh if we have to many clients just ignore it.
	for (size_t i = 0; i < MULTI_MAX_CLIENTS; i++) {
		if (mc->msc->clients[i] != NULL) {
			continue;
		}
		mc->msc->clients[i] = mc;
		break;
	}

	u_rt_info(                                         //
	    mc->urt,                                       //
	    msc->last_timings.predicted_display_time_ns,   //
	    msc->last_timings.predicted_display_period_ns, //
	    msc->last_timings.diff_ns);                    //

	os_mutex_unlock(&msc->list_and_timing_lock);

	*out_xcn = &mc->base;

	return XRT_SUCCESS;
}
