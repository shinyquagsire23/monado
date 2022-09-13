// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Multi client wrapper compositor.
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_multi
 */

#include "os/os_threading.h"
#include "xrt/xrt_gfx_native.h"

#include "os/os_time.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_wait.h"
#include "util/u_debug.h"
#include "util/u_trace_marker.h"
#include "util/u_distortion_mesh.h"

#include "multi/comp_multi_private.h"
#include "multi/comp_multi_interface.h"

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
 * Render thread.
 *
 */

static void
do_projection_layer(struct xrt_compositor *xc, struct multi_compositor *mc, struct multi_layer_entry *layer, uint32_t i)
{
	struct xrt_device *xdev = layer->xdev;
	struct xrt_swapchain *l_xcs = layer->xscs[0];
	struct xrt_swapchain *r_xcs = layer->xscs[1];

	if (l_xcs == NULL || r_xcs == NULL) {
		U_LOG_E("Invalid swap chain for projection layer #%u!", i);
		return;
	}

	if (xdev == NULL) {
		U_LOG_E("Invalid xdev for projection layer #%u!", i);
		return;
	}

	// Cast away
	struct xrt_layer_data *data = (struct xrt_layer_data *)&layer->data;

	xrt_comp_layer_stereo_projection(xc, xdev, l_xcs, r_xcs, data);
}

static void
do_projection_layer_depth(struct xrt_compositor *xc,
                          struct multi_compositor *mc,
                          struct multi_layer_entry *layer,
                          uint32_t i)
{
	struct xrt_device *xdev = layer->xdev;
	struct xrt_swapchain *l_xcs = layer->xscs[0];
	struct xrt_swapchain *r_xcs = layer->xscs[1];
	struct xrt_swapchain *l_d_xcs = layer->xscs[2];
	struct xrt_swapchain *r_d_xcs = layer->xscs[3];

	if (l_xcs == NULL || r_xcs == NULL || l_d_xcs == NULL || r_d_xcs == NULL) {
		U_LOG_E("Invalid swap chain for projection layer #%u!", i);
		return;
	}

	if (xdev == NULL) {
		U_LOG_E("Invalid xdev for projection layer #%u!", i);
		return;
	}

	// Cast away
	struct xrt_layer_data *data = (struct xrt_layer_data *)&layer->data;

	xrt_comp_layer_stereo_projection_depth(xc, xdev, l_xcs, r_xcs, l_d_xcs, r_d_xcs, data);
}

static bool
do_single(struct xrt_compositor *xc,
          struct multi_compositor *mc,
          struct multi_layer_entry *layer,
          uint32_t i,
          const char *name,
          struct xrt_device **out_xdev,
          struct xrt_swapchain **out_xcs,
          struct xrt_layer_data **out_data)
{
	struct xrt_device *xdev = layer->xdev;
	struct xrt_swapchain *xcs = layer->xscs[0];

	if (xcs == NULL) {
		U_LOG_E("Invalid swapchain for layer #%u '%s'!", i, name);
		return false;
	}

	if (xdev == NULL) {
		U_LOG_E("Invalid xdev for layer #%u '%s'!", i, name);
		return false;
	}

	// Cast away
	struct xrt_layer_data *data = (struct xrt_layer_data *)&layer->data;

	*out_xdev = xdev;
	*out_xcs = xcs;
	*out_data = data;

	return true;
}

static void
do_quad_layer(struct xrt_compositor *xc, struct multi_compositor *mc, struct multi_layer_entry *layer, uint32_t i)
{
	struct xrt_device *xdev = NULL;
	struct xrt_swapchain *xcs = NULL;
	struct xrt_layer_data *data = NULL;

	if (!do_single(xc, mc, layer, i, "quad", &xdev, &xcs, &data)) {
		return;
	}

	xrt_comp_layer_quad(xc, xdev, xcs, data);
}

static void
do_cube_layer(struct xrt_compositor *xc, struct multi_compositor *mc, struct multi_layer_entry *layer, uint32_t i)
{
	struct xrt_device *xdev = NULL;
	struct xrt_swapchain *xcs = NULL;
	struct xrt_layer_data *data = NULL;

	if (!do_single(xc, mc, layer, i, "cube", &xdev, &xcs, &data)) {
		return;
	}

	xrt_comp_layer_cube(xc, xdev, xcs, data);
}

static void
do_cylinder_layer(struct xrt_compositor *xc, struct multi_compositor *mc, struct multi_layer_entry *layer, uint32_t i)
{
	struct xrt_device *xdev = NULL;
	struct xrt_swapchain *xcs = NULL;
	struct xrt_layer_data *data = NULL;

	if (!do_single(xc, mc, layer, i, "cylinder", &xdev, &xcs, &data)) {
		return;
	}

	xrt_comp_layer_cylinder(xc, xdev, xcs, data);
}

static void
do_equirect1_layer(struct xrt_compositor *xc, struct multi_compositor *mc, struct multi_layer_entry *layer, uint32_t i)
{
	struct xrt_device *xdev = NULL;
	struct xrt_swapchain *xcs = NULL;
	struct xrt_layer_data *data = NULL;

	if (!do_single(xc, mc, layer, i, "equirect1", &xdev, &xcs, &data)) {
		return;
	}

	xrt_comp_layer_equirect1(xc, xdev, xcs, data);
}

static void
do_equirect2_layer(struct xrt_compositor *xc, struct multi_compositor *mc, struct multi_layer_entry *layer, uint32_t i)
{
	struct xrt_device *xdev = NULL;
	struct xrt_swapchain *xcs = NULL;
	struct xrt_layer_data *data = NULL;

	if (!do_single(xc, mc, layer, i, "equirect2", &xdev, &xcs, &data)) {
		return;
	}

	xrt_comp_layer_equirect2(xc, xdev, xcs, data);
}

static int
overlay_sort_func(const void *a, const void *b)
{
	struct multi_compositor *mc_a = *(struct multi_compositor **)a;
	struct multi_compositor *mc_b = *(struct multi_compositor **)b;

	if (mc_a->state.z_order < mc_b->state.z_order) {
		return -1;
	}

	if (mc_a->state.z_order > mc_b->state.z_order) {
		return 1;
	}

	return 0;
}

static void
log_frame_time_diff(uint64_t frame_time_ns, uint64_t display_time_ns)
{
	int64_t diff_ns = (int64_t)frame_time_ns - (int64_t)display_time_ns;
	bool late = false;
	if (diff_ns < 0) {
		diff_ns = -diff_ns;
		late = true;
	}

	U_LOG_W("Frame %s by %.2fms!", late ? "late" : "early", time_ns_to_ms_f(diff_ns));
}

static void
transfer_layers_locked(struct multi_system_compositor *msc, uint64_t display_time_ns)
{
	COMP_TRACE_MARKER();

	struct xrt_compositor *xc = &msc->xcn->base;

	struct multi_compositor *array[MULTI_MAX_CLIENTS] = {0};

	size_t count = 0;
	for (size_t k = 0; k < ARRAY_SIZE(array); k++) {
		if (msc->clients[k] == NULL) {
			continue;
		}

		array[count++] = msc->clients[k];

		// Even if it's not shown, make sure that frames are delivered.
		multi_compositor_deliver_any_frames(msc->clients[k], display_time_ns);
	}

	// Sort the stack array
	qsort(array, count, sizeof(struct multi_compositor *), overlay_sort_func);

	for (size_t k = 0; k < count; k++) {
		struct multi_compositor *mc = array[k];

		if (mc == NULL) {
			continue;
		}

		// None of the data in this slot is valid, don't check access it.
		if (!mc->delivered.active) {
			continue;
		}

		// The client isn't visible, do not submit it's layers.
		if (!mc->state.visible) {
			continue;
		}

		// Just in case.
		if (!mc->state.session_active) {
			U_LOG_W("Session is visible but not active.");
			continue;
		}

		uint64_t frame_time_ns = mc->delivered.display_time_ns;
		if (!time_is_within_half_ms(frame_time_ns, display_time_ns)) {
			log_frame_time_diff(frame_time_ns, display_time_ns);
		}

		for (uint32_t i = 0; i < mc->delivered.layer_count; i++) {
			struct multi_layer_entry *layer = &mc->delivered.layers[i];

			switch (layer->data.type) {
			case XRT_LAYER_STEREO_PROJECTION: do_projection_layer(xc, mc, layer, i); break;
			case XRT_LAYER_STEREO_PROJECTION_DEPTH: do_projection_layer_depth(xc, mc, layer, i); break;
			case XRT_LAYER_QUAD: do_quad_layer(xc, mc, layer, i); break;
			case XRT_LAYER_CUBE: do_cube_layer(xc, mc, layer, i); break;
			case XRT_LAYER_CYLINDER: do_cylinder_layer(xc, mc, layer, i); break;
			case XRT_LAYER_EQUIRECT1: do_equirect1_layer(xc, mc, layer, i); break;
			case XRT_LAYER_EQUIRECT2: do_equirect2_layer(xc, mc, layer, i); break;
			default: U_LOG_E("Unhandled layer type '%i'!", layer->data.type); break;
			}
		}
	}
}

static void
broadcast_timings_to_clients(struct multi_system_compositor *msc, uint64_t predicted_display_time_ns)
{
	COMP_TRACE_MARKER();

	os_mutex_lock(&msc->list_and_timing_lock);

	for (size_t i = 0; i < ARRAY_SIZE(msc->clients); i++) {
		struct multi_compositor *mc = msc->clients[i];
		if (mc == NULL) {
			continue;
		}

		os_mutex_lock(&mc->slot_lock);
		mc->slot_next_frame_display = predicted_display_time_ns;
		os_mutex_unlock(&mc->slot_lock);
	}

	os_mutex_unlock(&msc->list_and_timing_lock);
}

static void
broadcast_timings_to_pacers(struct multi_system_compositor *msc,
                            uint64_t predicted_display_time_ns,
                            uint64_t predicted_display_period_ns,
                            uint64_t diff_ns)
{
	COMP_TRACE_MARKER();

	os_mutex_lock(&msc->list_and_timing_lock);

	for (size_t i = 0; i < ARRAY_SIZE(msc->clients); i++) {
		struct multi_compositor *mc = msc->clients[i];
		if (mc == NULL) {
			continue;
		}

		u_pa_info(                       //
		    mc->upa,                     //
		    predicted_display_time_ns,   //
		    predicted_display_period_ns, //
		    diff_ns);                    //

		os_mutex_lock(&mc->slot_lock);
		mc->slot_next_frame_display = predicted_display_time_ns;
		os_mutex_unlock(&mc->slot_lock);
	}

	msc->last_timings.predicted_display_time_ns = predicted_display_time_ns;
	msc->last_timings.predicted_display_period_ns = predicted_display_period_ns;
	msc->last_timings.diff_ns = diff_ns;

	os_mutex_unlock(&msc->list_and_timing_lock);
}

static void
wait_frame(struct os_precise_sleeper *sleeper, struct xrt_compositor *xc, int64_t frame_id, uint64_t wake_up_time_ns)
{
	COMP_TRACE_MARKER();

	// Wait until the given wake up time.
	u_wait_until(sleeper, wake_up_time_ns);

	uint64_t now_ns = os_monotonic_get_ns();

	// Signal that we woke up.
	xrt_comp_mark_frame(xc, frame_id, XRT_COMPOSITOR_FRAME_POINT_WOKE, now_ns);
}

static void
update_session_state_locked(struct multi_system_compositor *msc)
{
	struct xrt_compositor *xc = &msc->xcn->base;

	//! @todo Don't make this a hack.
	enum xrt_view_type view_type = XRT_VIEW_TYPE_STEREO;

	switch (msc->sessions.state) {
	case MULTI_SYSTEM_STATE_INIT_WARM_START:
		U_LOG_I("Doing warm start, %u active app session(s).", (uint32_t)msc->sessions.active_count);

		// Produce at least one frame on init.
		msc->sessions.state = MULTI_SYSTEM_STATE_STOPPING;
		xrt_comp_begin_session(xc, view_type);
		break;

	case MULTI_SYSTEM_STATE_STOPPED:
		if (msc->sessions.active_count == 0) {
			break;
		}

		U_LOG_I("Starting native session, %u active app session(s).", (uint32_t)msc->sessions.active_count);
		msc->sessions.state = MULTI_SYSTEM_STATE_RUNNING;
		xrt_comp_begin_session(xc, view_type);
		break;

	case MULTI_SYSTEM_STATE_RUNNING:
		if (msc->sessions.active_count > 0) {
			break;
		}

		U_LOG_I("Stopping main session, %u active app session(s).", (uint32_t)msc->sessions.active_count);
		msc->sessions.state = MULTI_SYSTEM_STATE_STOPPING;
		break;

	case MULTI_SYSTEM_STATE_STOPPING:
		// Just in case
		if (msc->sessions.active_count > 0) {
			U_LOG_I("Restarting main session, %u active app session(s).",
			        (uint32_t)msc->sessions.active_count);
			msc->sessions.state = MULTI_SYSTEM_STATE_RUNNING;
			break;
		}

		U_LOG_I("Stopped main session, %u active app session(s).", (uint32_t)msc->sessions.active_count);
		msc->sessions.state = MULTI_SYSTEM_STATE_STOPPED;
		xrt_comp_end_session(xc);
		break;

	case MULTI_SYSTEM_STATE_INVALID:
	default:
		U_LOG_E("Got invalid state %u", msc->sessions.state);
		msc->sessions.state = MULTI_SYSTEM_STATE_STOPPING;
		assert(false);
	}
}

static int
multi_main_loop(struct multi_system_compositor *msc)
{
	COMP_TRACE_MARKER();

	os_thread_helper_name(&(msc->oth), "Multi-Compositor");

	struct xrt_compositor *xc = &msc->xcn->base;

	// For wait frame.
	struct os_precise_sleeper sleeper = {0};
	os_precise_sleeper_init(&sleeper);

	// Protect the thread state and the sessions state.
	os_thread_helper_lock(&msc->oth);

	while (os_thread_helper_is_running_locked(&msc->oth)) {

		// Updates msc->sessions.active depending on active client sessions.
		update_session_state_locked(msc);

		if (msc->sessions.state == MULTI_SYSTEM_STATE_STOPPED) {
			// Sleep and wait to be signaled.
			os_thread_helper_wait_locked(&msc->oth);

			// Loop back to running and session check.
			continue;
		}

		// Unlock the thread after the checks has been done.
		os_thread_helper_unlock(&msc->oth);

		int64_t frame_id = -1;
		uint64_t wake_up_time_ns = 0;
		uint64_t predicted_gpu_time_ns = 0;
		uint64_t predicted_display_time_ns = 0;
		uint64_t predicted_display_period_ns = 0;

		// Get the information for the next frame.
		xrt_comp_predict_frame(            //
		    xc,                            //
		    &frame_id,                     //
		    &wake_up_time_ns,              //
		    &predicted_gpu_time_ns,        //
		    &predicted_display_time_ns,    //
		    &predicted_display_period_ns); //

		// Do this as soon as we have the new display time.
		broadcast_timings_to_clients(msc, predicted_display_time_ns);

		// Now we can wait.
		wait_frame(&sleeper, xc, frame_id, wake_up_time_ns);

		uint64_t now_ns = os_monotonic_get_ns();
		uint64_t diff_ns = predicted_display_time_ns - now_ns;

		// Now we know the diff, broadcast to pacers.
		broadcast_timings_to_pacers(msc, predicted_display_time_ns, predicted_display_period_ns, diff_ns);

		xrt_comp_begin_frame(xc, frame_id);
		xrt_comp_layer_begin(xc, frame_id, 0, 0);

		// Make sure that the clients doesn't go away while we transfer layers.
		os_mutex_lock(&msc->list_and_timing_lock);
		transfer_layers_locked(msc, predicted_display_time_ns);
		os_mutex_unlock(&msc->list_and_timing_lock);

		xrt_comp_layer_commit(xc, frame_id, XRT_GRAPHICS_SYNC_HANDLE_INVALID);

		// Re-lock the thread for check in while statement.
		os_thread_helper_lock(&msc->oth);
	}

	// Clean up the sessions state.
	switch (msc->sessions.state) {
	case MULTI_SYSTEM_STATE_RUNNING:
	case MULTI_SYSTEM_STATE_STOPPING:
		U_LOG_I("Stopped native session, shutting down.");
		xrt_comp_end_session(xc);
		break;
	case MULTI_SYSTEM_STATE_STOPPED: break;
	default: assert(false);
	}

	os_thread_helper_unlock(&msc->oth);

	os_precise_sleeper_deinit(&sleeper);

	return 0;
}

static void *
thread_func(void *ptr)
{
	return (void *)(intptr_t)multi_main_loop((struct multi_system_compositor *)ptr);
}


/*
 *
 * System multi compositor functions.
 *
 */

static xrt_result_t
system_compositor_set_state(struct xrt_system_compositor *xsc, struct xrt_compositor *xc, bool visible, bool focused)
{
	struct multi_system_compositor *msc = multi_system_compositor(xsc);
	struct multi_compositor *mc = multi_compositor(xc);
	(void)msc;

	//! @todo Locking?
	if (mc->state.visible != visible || mc->state.focused != focused) {
		mc->state.visible = visible;
		mc->state.focused = focused;

		union xrt_compositor_event xce = {0};
		xce.type = XRT_COMPOSITOR_EVENT_STATE_CHANGE;
		xce.state.visible = visible;
		xce.state.focused = focused;

		multi_compositor_push_event(mc, &xce);
	}

	return XRT_SUCCESS;
}

static xrt_result_t
system_compositor_set_z_order(struct xrt_system_compositor *xsc, struct xrt_compositor *xc, int64_t z_order)
{
	struct multi_system_compositor *msc = multi_system_compositor(xsc);
	struct multi_compositor *mc = multi_compositor(xc);
	(void)msc;

	//! @todo Locking?
	mc->state.z_order = z_order;

	return XRT_SUCCESS;
}

static xrt_result_t
system_compositor_set_main_app_visibility(struct xrt_system_compositor *xsc, struct xrt_compositor *xc, bool visible)
{
	struct multi_system_compositor *msc = multi_system_compositor(xsc);
	struct multi_compositor *mc = multi_compositor(xc);
	(void)msc;

	union xrt_compositor_event xce = {0};
	xce.type = XRT_COMPOSITOR_EVENT_OVERLAY_CHANGE;
	xce.overlay.visible = visible;

	multi_compositor_push_event(mc, &xce);

	return XRT_SUCCESS;
}


/*
 *
 * System compositor functions.
 *
 */

static xrt_result_t
system_compositor_create_native_compositor(struct xrt_system_compositor *xsc,
                                           const struct xrt_session_info *xsi,
                                           struct xrt_compositor_native **out_xcn)
{
	struct multi_system_compositor *msc = multi_system_compositor(xsc);

	return multi_compositor_create(msc, xsi, out_xcn);
}

static void
system_compositor_destroy(struct xrt_system_compositor *xsc)
{
	struct multi_system_compositor *msc = multi_system_compositor(xsc);

	// Destroy the render thread first, destroy also stops the thread.
	os_thread_helper_destroy(&msc->oth);

	u_paf_destroy(&msc->upaf);

	xrt_comp_native_destroy(&msc->xcn);

	os_mutex_destroy(&msc->list_and_timing_lock);

	free(msc);
}


/*
 *
 * 'Exported' functions.
 *
 */

void
multi_system_compositor_update_session_status(struct multi_system_compositor *msc, bool active)
{
	os_thread_helper_lock(&msc->oth);

	if (active) {
		assert(msc->sessions.active_count < UINT32_MAX);
		msc->sessions.active_count++;

		// If the thread is sleeping wake it up.
		os_thread_helper_signal_locked(&msc->oth);
	} else {
		assert(msc->sessions.active_count > 0);
		msc->sessions.active_count--;
	}

	os_thread_helper_unlock(&msc->oth);
}

xrt_result_t
comp_multi_create_system_compositor(struct xrt_compositor_native *xcn,
                                    struct u_pacing_app_factory *upaf,
                                    const struct xrt_system_compositor_info *xsci,
                                    bool do_warm_start,
                                    struct xrt_system_compositor **out_xsysc)
{
	struct multi_system_compositor *msc = U_TYPED_CALLOC(struct multi_system_compositor);
	msc->base.create_native_compositor = system_compositor_create_native_compositor;
	msc->base.destroy = system_compositor_destroy;
	msc->xmcc.set_state = system_compositor_set_state;
	msc->xmcc.set_z_order = system_compositor_set_z_order;
	msc->xmcc.set_main_app_visibility = system_compositor_set_main_app_visibility;
	msc->base.xmcc = &msc->xmcc;
	msc->base.info = *xsci;
	msc->upaf = upaf;
	msc->xcn = xcn;
	msc->sessions.active_count = 0;
	msc->sessions.state = do_warm_start ? MULTI_SYSTEM_STATE_INIT_WARM_START : MULTI_SYSTEM_STATE_STOPPED;

	os_mutex_init(&msc->list_and_timing_lock);

	//! @todo Make the clients not go from IDLE to READY before we have completed a first frame.
	// Make sure there is at least some sort of valid frame data here.
	msc->last_timings.predicted_display_time_ns = os_monotonic_get_ns();   // As good as any time.
	msc->last_timings.predicted_display_period_ns = U_TIME_1MS_IN_NS * 16; // Just a wild guess.
	msc->last_timings.diff_ns = U_TIME_1MS_IN_NS * 5;                      // Make sure it's not zero at least.

	int ret = os_thread_helper_init(&msc->oth);
	if (ret < 0) {
		return XRT_ERROR_THREADING_INIT_FAILURE;
	}

	os_thread_helper_start(&msc->oth, thread_func, msc);

	*out_xsysc = &msc->base;

	return XRT_SUCCESS;
}
