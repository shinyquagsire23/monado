// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main compositor written using Vulkan implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup comp_main
 *
 *
 * begin_frame and end_frame delimit the application's work on graphics for a
 * single frame. end_frame updates our estimate of the current estimated app
 * graphics duration, as well as the "swap interval" for scheduling the
 * application.
 *
 * We have some known overhead work required to composite a frame: eventually
 * this may be measured as well. Overhead plus the estimated app render duration
 * is compared to the frame duration: if it's longer, then we go to a "swap
 * interval" of 2.
 *
 * wait_frame must be the one to produce the next predicted display time,
 * because we cannot distinguish two sequential wait_frame calls (an app
 * skipping a frame) from an OS scheduling blip causing the second wait_frame to
 * happen before the first begin_frame actually gets executed. It cannot use the
 * last display time in this computation for this reason. (Except perhaps to
 * align the period at a sub-frame level? e.g. should be a multiple of the frame
 * duration after the last displayed time).
 *
 * wait_frame should not actually produce the predicted display time until it's
 * done waiting: it should wake up once a frame and see what the current swap
 * interval suggests: this handles the case where end_frame changes the swap
 * interval from 2 to 1 during a wait_frame call. (That is, we should wait until
 * whichever is closer of the next vsync or the time we currently predict we
 * should release the app.)
 *
 * Sleeping can be a bit hairy: in general right now we'll use a combination of
 * operating system sleeps and busy-waits (for fine-grained waiting). Some
 * platforms provide vsync-related sync primitives that may get us closer to our
 * desired time. This is also convenient for the "wait until next frame"
 * behavior.
 */

#include "xrt/xrt_config_have.h"

#include "os/os_time.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_pacing.h"
#include "util/u_handles.h"
#include "util/u_trace_marker.h"
#include "util/u_pretty_print.h"
#include "util/u_distortion_mesh.h"
#include "util/u_verify.h"

#include "util/comp_vulkan.h"
#include "main/comp_compositor.h"
#include "main/comp_frame.h"

#ifdef XRT_FEATURE_WINDOW_PEEK
#include "main/comp_window_peek.h"
#endif

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


#define WINDOW_TITLE "Monado"

DEBUG_GET_ONCE_BOOL_OPTION(disable_deferred, "XRT_COMPOSITOR_DISABLE_DEFERRED", false)


/*
 *
 * Helper functions.
 *
 */

static double
ns_to_ms(int64_t ns)
{
	double ms = ((double)ns) * 1. / 1000. * 1. / 1000.;
	return ms;
}

static double
ts_ms(void)
{
	int64_t monotonic = os_monotonic_get_ns();
	return ns_to_ms(monotonic);
}

static struct vk_bundle *
get_vk(struct comp_compositor *c)
{
	return &c->base.vk;
}


/*
 *
 * Compositor functions.
 *
 */

static bool
compositor_init_window_post_vulkan(struct comp_compositor *c);
static bool
compositor_init_swapchain(struct comp_compositor *c);
static bool
compositor_init_renderer(struct comp_compositor *c);

static xrt_result_t
compositor_begin_session(struct xrt_compositor *xc, const struct xrt_begin_session_info *info)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_DEBUG(c, "BEGIN_SESSION");

	// clang-format off
	if (c->deferred_surface) {
		if (!compositor_init_window_post_vulkan(c) ||
		    !compositor_init_swapchain(c) ||
		    !compositor_init_renderer(c)) {
			COMP_ERROR(c, "Failed to init compositor %p", (void *)c);
			c->base.base.base.destroy(&c->base.base.base);

			return XRT_ERROR_VULKAN;
		}
		comp_target_set_title(c->target, WINDOW_TITLE);
		comp_renderer_add_debug_vars(c->r);
	}
	// clang-format on

	return XRT_SUCCESS;
}

static xrt_result_t
compositor_end_session(struct xrt_compositor *xc)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_DEBUG(c, "END_SESSION");

	if (c->deferred_surface) {
		// Make sure we don't have anything to destroy.
		comp_swapchain_shared_garbage_collect(&c->base.cscs);
		comp_renderer_destroy(&c->r);
#ifdef XRT_FEATURE_WINDOW_PEEK
		comp_window_peek_destroy(&c->peek);
#endif
		comp_target_destroy(&c->target);
	}

	return XRT_SUCCESS;
}

static xrt_result_t
compositor_predict_frame(struct xrt_compositor *xc,
                         int64_t *out_frame_id,
                         uint64_t *out_wake_time_ns,
                         uint64_t *out_predicted_gpu_time_ns,
                         uint64_t *out_predicted_display_time_ns,
                         uint64_t *out_predicted_display_period_ns)
{
	COMP_TRACE_MARKER();

	struct comp_compositor *c = comp_compositor(xc);

	COMP_SPEW(c, "PREDICT_FRAME");

	// A little bit easier to read.
	uint64_t interval_ns = (int64_t)c->settings.nominal_frame_interval_ns;

	comp_target_update_timings(c->target);

	assert(comp_frame_is_invalid_locked(&c->frame.waited));

	int64_t frame_id = -1;
	uint64_t wake_up_time_ns = 0;
	uint64_t present_slop_ns = 0;
	uint64_t desired_present_time_ns = 0;
	uint64_t predicted_display_time_ns = 0;
	comp_target_calc_frame_pacing(   //
	    c->target,                   //
	    &frame_id,                   //
	    &wake_up_time_ns,            //
	    &desired_present_time_ns,    //
	    &present_slop_ns,            //
	    &predicted_display_time_ns); //

	c->frame.waited.id = frame_id;
	c->frame.waited.desired_present_time_ns = desired_present_time_ns;
	c->frame.waited.present_slop_ns = present_slop_ns;
	c->frame.waited.predicted_display_time_ns = predicted_display_time_ns;

	*out_frame_id = frame_id;
	*out_wake_time_ns = wake_up_time_ns;
	*out_predicted_gpu_time_ns = desired_present_time_ns; // Not quite right but close enough.
	*out_predicted_display_time_ns = predicted_display_time_ns;
	*out_predicted_display_period_ns = interval_ns;

	return XRT_SUCCESS;
}

static xrt_result_t
compositor_mark_frame(struct xrt_compositor *xc,
                      int64_t frame_id,
                      enum xrt_compositor_frame_point point,
                      uint64_t when_ns)
{
	COMP_TRACE_MARKER();

	struct comp_compositor *c = comp_compositor(xc);

	COMP_SPEW(c, "MARK_FRAME %i", point);

	switch (point) {
	case XRT_COMPOSITOR_FRAME_POINT_WOKE:
		comp_target_mark_wake_up(c->target, frame_id, when_ns);
		return XRT_SUCCESS;
	default: assert(false);
	}
	return XRT_ERROR_VULKAN;
}

static xrt_result_t
compositor_begin_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_SPEW(c, "BEGIN_FRAME");
	c->app_profiling.last_begin = os_monotonic_get_ns();
	return XRT_SUCCESS;
}

static xrt_result_t
compositor_discard_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_SPEW(c, "DISCARD_FRAME at %8.3fms", ts_ms());
	return XRT_SUCCESS;
}

static void
do_graphics_layers(struct comp_compositor *c)
{
	// Always zero for now.
	uint32_t layer_count = c->base.slot.layer_count;

	comp_renderer_destroy_layers(c->r);

	if (c->base.slot.one_projection_layer_fast_path) {
		return;
	}

	comp_renderer_allocate_layers(c->r, layer_count);

	for (uint32_t i = 0; i < layer_count; i++) {
		struct comp_layer *layer = &c->base.slot.layers[i];
		struct xrt_layer_data *data = &layer->data;

		COMP_SPEW(c, "LAYER_COMMIT (%d) predicted display time: %8.3fms", i, ns_to_ms(data->timestamp));

		switch (data->type) {
		case XRT_LAYER_QUAD: {
			struct xrt_layer_quad_data *quad = &layer->data.quad;
			struct comp_swapchain_image *image;
			image = &layer->sc_array[0]->images[quad->sub.image_index];
			comp_renderer_set_quad_layer(c->r, i, image, data);
		} break;
		case XRT_LAYER_STEREO_PROJECTION: {
			struct xrt_layer_stereo_projection_data *stereo = &data->stereo;
			struct comp_swapchain_image *right;
			struct comp_swapchain_image *left;
			left = &layer->sc_array[0]->images[stereo->l.sub.image_index];
			right = &layer->sc_array[1]->images[stereo->r.sub.image_index];

			comp_renderer_set_projection_layer(c->r, i, left, right, data);
		} break;
		case XRT_LAYER_STEREO_PROJECTION_DEPTH: {
			struct xrt_layer_stereo_projection_depth_data *stereo = &data->stereo_depth;
			struct comp_swapchain_image *right;
			struct comp_swapchain_image *left;
			left = &layer->sc_array[0]->images[stereo->l.sub.image_index];
			right = &layer->sc_array[1]->images[stereo->r.sub.image_index];

			//! @todo: Make use of stereo->l_d and stereo->r_d

			comp_renderer_set_projection_layer(c->r, i, left, right, data);
		} break;
		case XRT_LAYER_CYLINDER: {
			struct xrt_layer_cylinder_data *cyl = &layer->data.cylinder;
			struct comp_swapchain_image *image;
			image = &layer->sc_array[0]->images[cyl->sub.image_index];
			comp_renderer_set_cylinder_layer(c->r, i, image, data);
		} break;
#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT1
		case XRT_LAYER_EQUIRECT1: {
			struct xrt_layer_equirect1_data *eq = &layer->data.equirect1;
			struct comp_swapchain_image *image;
			image = &layer->sc_array[0]->images[eq->sub.image_index];
			comp_renderer_set_equirect1_layer(c->r, i, image, data);
		} break;
#endif
#ifdef XRT_FEATURE_OPENXR_LAYER_EQUIRECT2
		case XRT_LAYER_EQUIRECT2: {
			struct xrt_layer_equirect2_data *eq = &layer->data.equirect2;
			struct comp_swapchain_image *image;
			image = &layer->sc_array[0]->images[eq->sub.image_index];
			comp_renderer_set_equirect2_layer(c->r, i, image, data);
		} break;
#endif
#ifdef XRT_FEATURE_OPENXR_LAYER_CUBE
		case XRT_LAYER_CUBE: {
			struct xrt_layer_cube_data *cu = &layer->data.cube;
			struct comp_swapchain_image *image;
			image = &layer->sc_array[0]->images[cu->sub.image_index];
			comp_renderer_set_cube_layer(c->r, i, image, data);
		} break;
#endif

#ifndef XRT_FEATURE_OPENXR_LAYER_EQUIRECT1
		case XRT_LAYER_EQUIRECT1:
#endif
#ifndef XRT_FEATURE_OPENXR_LAYER_EQUIRECT2
		case XRT_LAYER_EQUIRECT2:
#endif
#ifndef XRT_FEATURE_OPENXR_LAYER_CUBE
		case XRT_LAYER_CUBE:
#endif
		default:
			// Should never end up here.
			assert(false);
		}
	}
}

/*!
 * We have a fast path for single projection layer that goes directly
 * to the distortion shader, so no need to use the layer renderer.
 */
static bool
can_do_one_projection_layer_fast_path(struct comp_compositor *c)
{
	if (c->base.slot.layer_count != 1) {
		return false;
	}

	struct comp_layer *layer = &c->base.slot.layers[0];
	enum xrt_layer_type type = layer->data.type;

	// Handled by the distortion shader.
	if (type != XRT_LAYER_STEREO_PROJECTION && //
	    type != XRT_LAYER_STEREO_PROJECTION_DEPTH) {
		return false;
	}

	return true;
}

static xrt_result_t
compositor_layer_commit(struct xrt_compositor *xc, xrt_graphics_sync_handle_t sync_handle)
{
	COMP_TRACE_MARKER();

	struct comp_compositor *c = comp_compositor(xc);

	COMP_SPEW(c, "LAYER_COMMIT at %8.3fms", ts_ms());

	/*
	 * We have a fast path for single projection layer that goes directly
	 * to the distortion shader, so no need to use the layer renderer.
	 */
	bool fast_path = can_do_one_projection_layer_fast_path(c) && !c->mirroring_to_debug_gui && !c->peek;
	c->base.slot.one_projection_layer_fast_path = fast_path;


	u_graphics_sync_unref(&sync_handle);

	if (!c->settings.use_compute) {
		do_graphics_layers(c);
	}

	comp_renderer_draw(c->r);

	u_frame_times_widget_push_sample(&c->compositor_frame_times, os_monotonic_get_ns());

	// Record the time of this frame.
	c->last_frame_time_ns = os_monotonic_get_ns();
	c->app_profiling.last_end = c->last_frame_time_ns;


	COMP_SPEW(c, "LAYER_COMMIT finished drawing at %8.3fms", ns_to_ms(c->last_frame_time_ns));

	// Now is a good point to garbage collect.
	comp_swapchain_shared_garbage_collect(&c->base.cscs);

	return XRT_SUCCESS;
}

static xrt_result_t
compositor_poll_events(struct xrt_compositor *xc, union xrt_compositor_event *out_xce)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_SPEW(c, "POLL_EVENTS");

	U_ZERO(out_xce);

	switch (c->state) {
	case COMP_STATE_UNINITIALIZED:
		COMP_ERROR(c, "Polled uninitialized compositor");
		out_xce->state.type = XRT_COMPOSITOR_EVENT_NONE;
		break;
	case COMP_STATE_READY: out_xce->state.type = XRT_COMPOSITOR_EVENT_NONE; break;
	case COMP_STATE_PREPARED:
		COMP_DEBUG(c, "PREPARED -> VISIBLE");
		out_xce->state.type = XRT_COMPOSITOR_EVENT_STATE_CHANGE;
		out_xce->state.visible = true;
		c->state = COMP_STATE_VISIBLE;
		break;
	case COMP_STATE_VISIBLE:
		COMP_DEBUG(c, "VISIBLE -> FOCUSED");
		out_xce->state.type = XRT_COMPOSITOR_EVENT_STATE_CHANGE;
		out_xce->state.visible = true;
		out_xce->state.focused = true;
		c->state = COMP_STATE_FOCUSED;
		break;
	case COMP_STATE_FOCUSED:
		// No more transitions.
		out_xce->state.type = XRT_COMPOSITOR_EVENT_NONE;
		break;
	}

	return XRT_SUCCESS;
}

static void
compositor_destroy(struct xrt_compositor *xc)
{
	struct comp_compositor *c = comp_compositor(xc);
	struct vk_bundle *vk = get_vk(c);

	COMP_DEBUG(c, "COMP_DESTROY");

	// Make sure we don't have anything to destroy.
	comp_swapchain_shared_garbage_collect(&c->base.cscs);

	// Must be destroyed before Vulkan.
	comp_swapchain_shared_destroy(&c->base.cscs, vk);

	comp_renderer_destroy(&c->r);

#ifdef XRT_FEATURE_WINDOW_PEEK
	comp_window_peek_destroy(&c->peek);
#endif

	// Does NULL checking.
	comp_target_destroy(&c->target);

	// Only depends on vk_bundle and shaders.
	render_resources_close(&c->nr);

	// As long as vk_bundle is valid it's safe to call this function.
	render_shaders_close(&c->shaders, vk);

	if (vk->device != VK_NULL_HANDLE) {
		vk->vkDestroyDevice(vk->device, NULL);
		vk->device = VK_NULL_HANDLE;
	}

	vk_deinit_mutex(vk);

	if (vk->instance != VK_NULL_HANDLE) {
		vk->vkDestroyInstance(vk->instance, NULL);
		vk->instance = VK_NULL_HANDLE;
	}

	u_var_remove_root(c);

	u_frame_times_widget_teardown(&c->compositor_frame_times);

	comp_base_fini(&c->base);

	free(c);
}


/*
 *
 * xdev functions.
 *
 */

static bool
compositor_check_and_prepare_xdev(struct comp_compositor *c, struct xrt_device *xdev)
{
	COMP_TRACE_MARKER();

	// clang-format off
	bool has_none = (xdev->hmd->distortion.models & XRT_DISTORTION_MODEL_NONE) != 0;
	bool has_meshuv = (xdev->hmd->distortion.models & XRT_DISTORTION_MODEL_MESHUV) != 0;
	bool has_compute = (xdev->hmd->distortion.models & XRT_DISTORTION_MODEL_COMPUTE) != 0;
	// clang-format on

	// Everything is okay! :D
	if (has_meshuv) {
		return true;
	}

	if (!has_none && !has_compute) {
		COMP_ERROR(c, "The xdev '%s' didn't have none nor compute distortion.", xdev->str);
		return false;
	}

	COMP_WARN(c,
	          "Had to fill in meshuv on xdev '%s', "
	          "this should be done in the driver.",
	          xdev->str);

	u_distortion_mesh_fill_in_compute(xdev);

	// clang-format off
	has_meshuv = (xdev->hmd->distortion.models & XRT_DISTORTION_MODEL_MESHUV) != 0;
	// clang-format on

	if (has_meshuv) {
		return true;
	}

	COMP_ERROR(c, "Failed to fill in meshuv on the xdev '%s'.", xdev->str);

	return false;
}


/*
 *
 * Vulkan functions.
 *
 */

// If any of these lists are updated, please also update the appropriate column
// in `vulkan-extensions.md`

static const char *instance_extensions_common[] = {
    COMP_INSTANCE_EXTENSIONS_COMMON,
};

static const char *optional_instance_extensions[] = {
#ifdef VK_EXT_swapchain_colorspace
    VK_EXT_SWAPCHAIN_COLORSPACE_EXTENSION_NAME,
#endif
#ifdef VK_EXT_display_surface_counter
    VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME,
#endif
};

// Note: Keep synchronized with comp_vk_glue - we should have everything they
// do, plus VK_KHR_SWAPCHAIN_EXTENSION_NAME
static const char *required_device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,                 //
    VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,      //
    VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,            //
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,           //
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,        //
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, //

// Platform version of "external_memory"
#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_FD)
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
    VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,

#elif defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_WIN32_HANDLE)
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,

#else
#error "Need port!"
#endif

// Platform version of "external_fence" and "external_semaphore"
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD) // Optional

#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
    VK_KHR_EXTERNAL_FENCE_WIN32_EXTENSION_NAME,

#else
#error "Need port!"
#endif
};

static const char *optional_device_extensions[] = {
    VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME, //
    VK_EXT_GLOBAL_PRIORITY_EXTENSION_NAME,   //

// Platform version of "external_fence" and "external_semaphore"
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME, //
    VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,     //

#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE) // Not optional

#else
#error "Need port!"
#endif

#ifdef VK_KHR_format_feature_flags2
    VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME,
#endif
#ifdef VK_KHR_global_priority
    VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME,
#endif
#ifdef VK_KHR_image_format_list
    VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
#endif
#ifdef VK_KHR_maintenance1
    VK_KHR_MAINTENANCE_1_EXTENSION_NAME,
#endif
#ifdef VK_KHR_maintenance2
    VK_KHR_MAINTENANCE_2_EXTENSION_NAME,
#endif
#ifdef VK_KHR_timeline_semaphore
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
#endif
#ifdef VK_EXT_calibrated_timestamps
    VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
#endif
#ifdef VK_EXT_robustness2
    VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
#endif
#ifdef VK_EXT_display_control
    VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME,
#endif
};

static bool
compositor_init_vulkan(struct comp_compositor *c)
{
	COMP_TRACE_MARKER();

	assert(c->target_factory != NULL);

	struct vk_bundle *vk = get_vk(c);


	/*
	 * Instance extensions.
	 */

	struct u_string_list *required_instance_ext_list = u_string_list_create();
	struct u_string_list *optional_instance_ext_list = u_string_list_create();

	// Every backend needs at least the common extensions.
	u_string_list_append_array(                  //
	    required_instance_ext_list,              //
	    instance_extensions_common,              //
	    ARRAY_SIZE(instance_extensions_common)); //

	// Add per target required extensions.
	u_string_list_append_array(                                //
	    required_instance_ext_list,                            //
	    c->target_factory->required_instance_extensions,       //
	    c->target_factory->required_instance_extension_count); //

	// Optional instance extensions.
	u_string_list_append_array(                    //
	    optional_instance_ext_list,                //
	    optional_instance_extensions,              //
	    ARRAY_SIZE(optional_instance_extensions)); //


	/*
	 * Device extensions.
	 */

	struct u_string_list *required_device_extension_list = u_string_list_create();
	struct u_string_list *optional_device_extension_list = u_string_list_create();

	// Required device extensions.
	u_string_list_append_array(                  //
	    required_device_extension_list,          //
	    required_device_extensions,              //
	    ARRAY_SIZE(required_device_extensions)); //

	// Optional device extensions.
	u_string_list_append_array(                  //
	    optional_device_extension_list,          //
	    optional_device_extensions,              //
	    ARRAY_SIZE(optional_device_extensions)); //


	/*
	 * Create the device.
	 */

	struct comp_vulkan_arguments vk_args = {
	    .get_instance_proc_address = vkGetInstanceProcAddr,
	    .required_instance_version = VK_MAKE_VERSION(1, 0, 0),
	    .required_instance_extensions = required_instance_ext_list,
	    .optional_instance_extensions = optional_instance_ext_list,
	    .required_device_extensions = required_device_extension_list,
	    .optional_device_extensions = optional_device_extension_list,
	    .log_level = c->settings.log_level,
	    .only_compute_queue = c->settings.use_compute,
	    .selected_gpu_index = c->settings.selected_gpu_index,
	    .client_gpu_index = c->settings.client_gpu_index,
	    .timeline_semaphore = true, // Flag is optional, not a hard requirement.
	};

	struct comp_vulkan_results vk_res = {0};
	bool bundle_ret = comp_vulkan_init_bundle(vk, &vk_args, &vk_res);

	u_string_list_destroy(&required_instance_ext_list);
	u_string_list_destroy(&optional_instance_ext_list);
	u_string_list_destroy(&required_device_extension_list);
	u_string_list_destroy(&optional_device_extension_list);

	if (!bundle_ret) {
		return false;
	}

	// clang-format off
	static_assert(ARRAY_SIZE(vk_res.client_gpu_deviceUUID.data) == XRT_UUID_SIZE, "array size mismatch");
	static_assert(ARRAY_SIZE(vk_res.selected_gpu_deviceUUID.data) == XRT_UUID_SIZE, "array size mismatch");
	static_assert(ARRAY_SIZE(vk_res.client_gpu_deviceUUID.data) == ARRAY_SIZE(c->settings.client_gpu_deviceUUID.data), "array size mismatch");
	static_assert(ARRAY_SIZE(vk_res.selected_gpu_deviceUUID.data) == ARRAY_SIZE(c->settings.selected_gpu_deviceUUID.data), "array size mismatch");
	static_assert(ARRAY_SIZE(vk_res.client_gpu_deviceLUID.data) == XRT_LUID_SIZE, "array size mismatch");
	static_assert(ARRAY_SIZE(vk_res.client_gpu_deviceLUID.data) == ARRAY_SIZE(c->settings.client_gpu_deviceLUID.data), "array size mismatch");
	// clang-format on

	c->settings.client_gpu_deviceUUID = vk_res.client_gpu_deviceUUID;
	c->settings.selected_gpu_deviceUUID = vk_res.selected_gpu_deviceUUID;
	c->settings.client_gpu_index = vk_res.client_gpu_index;
	c->settings.selected_gpu_index = vk_res.selected_gpu_index;
	c->settings.client_gpu_deviceLUID = vk_res.client_gpu_deviceLUID;
	c->settings.client_gpu_deviceLUID_valid = vk_res.client_gpu_deviceLUID_valid;

	// Tie the lifetimes of swapchains to Vulkan.
	xrt_result_t xret = comp_swapchain_shared_init(&c->base.cscs, vk);
	if (xret != XRT_SUCCESS) {
		return false;
	}

	return true;
}


/*
 *
 * Other functions.
 *
 */

const struct comp_target_factory *ctfs[] = {
#if defined VK_USE_PLATFORM_WAYLAND_KHR && defined XRT_HAVE_WAYLAND_DIRECT
    &comp_target_factory_direct_wayland,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    &comp_target_factory_wayland,
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    &comp_target_factory_direct_randr,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
    &comp_target_factory_xcb,
#endif
#ifdef XRT_OS_ANDROID
    &comp_target_factory_android,
#endif
#ifdef XRT_OS_WINDOWS
    &comp_target_factory_mswin,
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    &comp_target_factory_direct_nvidia,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
    &comp_target_factory_vk_display,
#endif
};

static void
error_msg_with_list(struct comp_compositor *c, const char *msg)
{
	struct u_pp_sink_stack_only sink;
	u_pp_delegate_t dg = u_pp_sink_stack_only_init(&sink);
	u_pp(dg, "%s, available targets:", msg);
	for (size_t i = 0; i < ARRAY_SIZE(ctfs); i++) {
		u_pp(dg, "\n\t%s: %s", ctfs[i]->identifier, ctfs[i]->name);
	}

	COMP_ERROR(c, "%s", sink.buffer);
}

static bool
compositor_check_deferred(struct comp_compositor *c, const struct comp_target_factory *ctf)
{
	if (debug_get_bool_option_disable_deferred()) {
		COMP_DEBUG(c, "Deferred window initialization globally disabled!");
		return false;
	}

	if (!ctf->is_deferred) {
		return false; // It is not deferred but that's okay.
	}

	COMP_DEBUG(c, "Deferred target backend %s selected!", ctf->name);

	c->target_factory = ctf;
	c->deferred_surface = true;

	return true;
}

static bool
compositor_try_window(struct comp_compositor *c, const struct comp_target_factory *ctf)
{
	COMP_TRACE_MARKER();

	struct comp_target *ct = NULL;

	if (!ctf->create_target(ctf, c, &ct)) {
		return false;
	}

	if (!comp_target_init_pre_vulkan(ct)) {
		ct->destroy(ct);
		return false;
	}

	COMP_DEBUG(c, "Target backend %s initialized!", ct->name);

	c->target_factory = ctf;
	c->target = ct;

	return true;
}

static bool
select_target_factory_from_settings(struct comp_compositor *c, const struct comp_target_factory **out_ctf)
{
	const char *identifier = c->settings.target_identifier;

	if (identifier == NULL) {
		return true; // Didn't ask for a target, all ok.
	}

	for (size_t i = 0; i < ARRAY_SIZE(ctfs); i++) {
		const struct comp_target_factory *ctf = ctfs[i];

		if (strcmp(ctf->identifier, identifier) == 0) {
			*out_ctf = ctf;
			return true;
		}
	}

	char buffer[256];
	snprintf(buffer, ARRAY_SIZE(buffer), "Could not find target factory with identifier '%s'", identifier);
	error_msg_with_list(c, buffer);

	return false; // User asked for a target that we couldn't find, error.
}

static bool
select_target_factory_by_detecting(struct comp_compositor *c, const struct comp_target_factory **out_ctf)
{
	for (size_t i = 0; i < ARRAY_SIZE(ctfs); i++) {
		const struct comp_target_factory *ctf = ctfs[i];

		if (comp_target_factory_detect(ctf, c)) {
			*out_ctf = ctf;
			return true;
		}
	}

	return true; // Didn't detect a target, but that's ok.
}

static bool
compositor_init_window_pre_vulkan(struct comp_compositor *c, const struct comp_target_factory *selected_ctf)
{
	COMP_TRACE_MARKER();

	if (selected_ctf == NULL && !select_target_factory_from_settings(c, &selected_ctf)) {
		return false; // Error!
	}

	if (selected_ctf == NULL && !select_target_factory_by_detecting(c, &selected_ctf)) {
		return false; // Error!
	}

	if (selected_ctf != NULL) {
		// We have selected a target factory, but it needs Vulkan.
		if (selected_ctf->requires_vulkan_for_create) {
			COMP_INFO(c, "Selected %s backend!", selected_ctf->name);
			c->target_factory = selected_ctf;
			return true;
		}

		if (compositor_check_deferred(c, selected_ctf)) {
			return true;
		}

		if (!compositor_try_window(c, selected_ctf)) {
			COMP_ERROR(c, "Failed to init %s backend!", selected_ctf->name);
			return false;
		}

		return true;
	}

	for (size_t i = 0; i < ARRAY_SIZE(ctfs); i++) {
		const struct comp_target_factory *ctf = ctfs[i];

		// Skip targets that requires Vulkan.
		if (ctf->requires_vulkan_for_create) {
			continue;
		}

		if (compositor_check_deferred(c, ctf)) {
			return true;
		}

		if (compositor_try_window(c, ctf)) {
			return true;
		}
	}

	// Nothing worked, giving up.
	error_msg_with_list(c, "Failed to create any target");

	return false;
}

static bool
compositor_init_window_post_vulkan(struct comp_compositor *c)
{
	COMP_TRACE_MARKER();

	assert(c->target_factory != NULL);

	if (c->target != NULL) {
		return true;
	}

	return compositor_try_window(c, c->target_factory);
}

static bool
compositor_init_swapchain(struct comp_compositor *c)
{
	COMP_TRACE_MARKER();

	assert(c->target != NULL);
	assert(c->target_factory != NULL);

	if (comp_target_init_post_vulkan(c->target,                   //
	                                 c->settings.preferred.width, //
	                                 c->settings.preferred.height)) {
		return true;
	}

	COMP_ERROR(c, "Window init_swapchain failed!");

	comp_target_destroy(&c->target);

	return false;
}

static bool
compositor_init_render_resources(struct comp_compositor *c)
{
	COMP_TRACE_MARKER();

	struct vk_bundle *vk = get_vk(c);

	if (!render_shaders_load(&c->shaders, vk)) {
		return false;
	}

	if (!render_resources_init(&c->nr, &c->shaders, get_vk(c), c->xdev)) {
		return false;
	}

	return true;
}

static bool
compositor_init_renderer(struct comp_compositor *c)
{
	COMP_TRACE_MARKER();

	c->r = comp_renderer_create(c);

#ifdef XRT_FEATURE_WINDOW_PEEK
	c->peek = comp_window_peek_create(c);
#else
	c->peek = NULL;
#endif

	return c->r != NULL;
}

xrt_result_t
comp_main_create_system_compositor(struct xrt_device *xdev,
                                   const struct comp_target_factory *ctf,
                                   struct xrt_system_compositor **out_xsysc)
{
	COMP_TRACE_MARKER();

	struct comp_compositor *c = U_TYPED_CALLOC(struct comp_compositor);

	c->base.base.base.begin_session = compositor_begin_session;
	c->base.base.base.end_session = compositor_end_session;
	c->base.base.base.predict_frame = compositor_predict_frame;
	c->base.base.base.mark_frame = compositor_mark_frame;
	c->base.base.base.begin_frame = compositor_begin_frame;
	c->base.base.base.discard_frame = compositor_discard_frame;
	c->base.base.base.layer_commit = compositor_layer_commit;
	c->base.base.base.poll_events = compositor_poll_events;
	c->base.base.base.destroy = compositor_destroy;
	c->frame.waited.id = -1;
	c->frame.rendering.id = -1;
	c->xdev = xdev;

	COMP_DEBUG(c, "Doing init %p", (void *)c);

	// Do this as early as possible.
	comp_base_init(&c->base);

	// Init the settings to default.
	comp_settings_init(&c->settings, xdev);

	c->last_frame_time_ns = os_monotonic_get_ns();

	double scale = c->settings.viewport_scale;

	if (scale > 2.0) {
		scale = 2.0;
		COMP_DEBUG(c, "Clamped scale to 200%%\n");
	}

	uint32_t w0 = (uint32_t)(xdev->hmd->views[0].display.w_pixels * scale);
	uint32_t h0 = (uint32_t)(xdev->hmd->views[0].display.h_pixels * scale);
	uint32_t w1 = (uint32_t)(xdev->hmd->views[1].display.w_pixels * scale);
	uint32_t h1 = (uint32_t)(xdev->hmd->views[1].display.h_pixels * scale);

	uint32_t w0_2 = xdev->hmd->views[0].display.w_pixels * 2;
	uint32_t h0_2 = xdev->hmd->views[0].display.h_pixels * 2;
	uint32_t w1_2 = xdev->hmd->views[1].display.w_pixels * 2;
	uint32_t h1_2 = xdev->hmd->views[1].display.h_pixels * 2;

	c->view_extents.width = w0;
	c->view_extents.height = h0;

	// Need to select window backend before creating Vulkan, then
	// swapchain will initialize the window fully and the swapchain,
	// and finally the renderer is created which renders to
	// window/swapchain.

	// clang-format off
	if (!compositor_check_and_prepare_xdev(c, xdev) ||
	    !compositor_init_window_pre_vulkan(c, ctf) ||
	    !compositor_init_vulkan(c) ||
	    !compositor_init_render_resources(c)) {
		COMP_ERROR(c, "Failed to init compositor %p", (void *)c);
		c->base.base.base.destroy(&c->base.base.base);

		return XRT_ERROR_VULKAN;
	}

	if (!c->deferred_surface) {
		if (!compositor_init_window_post_vulkan(c) ||
		    !compositor_init_swapchain(c) ||
		    !compositor_init_renderer(c)) {
			COMP_ERROR(c, "Failed to init compositor %p", (void*)c);
			c->base.base.base.destroy(&c->base.base.base);

			return XRT_ERROR_VULKAN;
		}
		comp_target_set_title(c->target, WINDOW_TITLE);
	}
	// clang-format on

	COMP_DEBUG(c, "Done %p", (void *)c);

	/*!
	 * @todo Support more like, depth/float formats etc,
	 * remember to update the GL client as well.
	 */

	struct xrt_compositor_info *info = &c->base.base.base.info;


	/*
	 * Formats.
	 */

	struct comp_vulkan_formats formats = {0};
	comp_vulkan_formats_check(get_vk(c), &formats);
	comp_vulkan_formats_copy_to_info(&formats, info);
	comp_vulkan_formats_log(c->settings.log_level, &formats);


	/*
	 * Rest of info.
	 */

	struct xrt_system_compositor_info sys_info_storage = {0};
	struct xrt_system_compositor_info *sys_info = &sys_info_storage;

	// Required by OpenXR spec.
	sys_info->max_layers = 16;
	sys_info->compositor_vk_deviceUUID = c->settings.selected_gpu_deviceUUID;
	sys_info->client_vk_deviceUUID = c->settings.client_gpu_deviceUUID;
	sys_info->client_d3d_deviceLUID = c->settings.client_gpu_deviceLUID;
	sys_info->client_d3d_deviceLUID_valid = c->settings.client_gpu_deviceLUID_valid;

	// clang-format off
	sys_info->views[0].recommended.width_pixels  = w0;
	sys_info->views[0].recommended.height_pixels = h0;
	sys_info->views[0].recommended.sample_count  = 1;
	sys_info->views[0].max.width_pixels          = w0_2;
	sys_info->views[0].max.height_pixels         = h0_2;
	sys_info->views[0].max.sample_count          = 1;

	sys_info->views[1].recommended.width_pixels  = w1;
	sys_info->views[1].recommended.height_pixels = h1;
	sys_info->views[1].recommended.sample_count  = 1;
	sys_info->views[1].max.width_pixels          = w1_2;
	sys_info->views[1].max.height_pixels         = h1_2;
	sys_info->views[1].max.sample_count          = 1;
	// clang-format on

	// If we can add e.g. video pass-through capabilities, we may need to change (augment) this list.
	// Just copying it directly right now.
	assert(xdev->hmd->blend_mode_count <= XRT_MAX_DEVICE_BLEND_MODES);
	assert(xdev->hmd->blend_mode_count != 0);
	assert(xdev->hmd->blend_mode_count <= ARRAY_SIZE(sys_info->supported_blend_modes));
	for (size_t i = 0; i < xdev->hmd->blend_mode_count; ++i) {
		assert(u_verify_blend_mode_valid(xdev->hmd->blend_modes[i]));
		sys_info->supported_blend_modes[i] = xdev->hmd->blend_modes[i];
	}
	sys_info->supported_blend_mode_count = (uint8_t)xdev->hmd->blend_mode_count;

	u_var_add_root(c, "Compositor", true);

	float target_frame_time_ms = (float)ns_to_ms(c->settings.nominal_frame_interval_ns);
	u_frame_times_widget_init(&c->compositor_frame_times, target_frame_time_ms, 10.f);

	u_var_add_ro_f32(c, &c->compositor_frame_times.fps, "FPS (Compositor)");
	u_var_add_bool(c, &c->debug.atw_off, "Debug: ATW OFF");
	u_var_add_f32_timing(c, c->compositor_frame_times.debug_var, "Frame Times (Compositor)");


	//! @todo: Query all supported refresh rates of the current mode
	sys_info->num_refresh_rates = 1;
	sys_info->refresh_rates[0] = (float)(1. / time_ns_to_s(c->settings.nominal_frame_interval_ns));

	// Needs to be delayed until after compositor's u_var has been setup.
	if (!c->deferred_surface) {
		comp_renderer_add_debug_vars(c->r);
	}

	c->state = COMP_STATE_READY;

	// Standard app pacer.
	struct u_pacing_app_factory *upaf = NULL;
	xrt_result_t xret = u_pa_factory_create(&upaf);
	assert(xret == XRT_SUCCESS && upaf != NULL);

	return comp_multi_create_system_compositor(&c->base.base, upaf, sys_info, !c->deferred_surface, out_xsysc);
}
