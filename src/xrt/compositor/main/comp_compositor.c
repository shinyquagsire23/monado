// Copyright 2019-2021, Collabora, Ltd.
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

#include "xrt/xrt_gfx_native.h"
#include "xrt/xrt_config_have.h"

#include "os/os_time.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_pacing.h"
#include "util/u_handles.h"
#include "util/u_trace_marker.h"
#include "util/u_distortion_mesh.h"
#include "util/u_verify.h"

#include "util/comp_vulkan.h"
#include "main/comp_compositor.h"

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


/*
 *
 * Helper functions.
 *
 */

#define CVK_ERROR(C, FUNC, MSG, RET) COMP_ERROR(C, FUNC ": %s\n\t" MSG, vk_result_string(RET));

static double
ns_to_ms(int64_t ns)
{
	double ms = ((double)ns) * 1. / 1000. * 1. / 1000.;
	return ms;
}

static double
ts_ms()
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

static xrt_result_t
compositor_begin_session(struct xrt_compositor *xc, enum xrt_view_type type)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_DEBUG(c, "BEGIN_SESSION");
	return XRT_SUCCESS;
}

static xrt_result_t
compositor_end_session(struct xrt_compositor *xc)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_DEBUG(c, "END_SESSION");
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

	assert(c->frame.waited.id == -1);

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
compositor_layer_commit(struct xrt_compositor *xc, int64_t frame_id, xrt_graphics_sync_handle_t sync_handle)
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
	comp_swapchain_garbage_collect(&c->base.cscgc);

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
	comp_swapchain_garbage_collect(&c->base.cscgc);

	comp_renderer_destroy(&c->r);

#ifdef XRT_FEATURE_WINDOW_PEEK
	comp_window_peek_destroy(&c->peek);
#endif

	render_resources_close(&c->nr);

	// As long as vk_bundle is valid it's safe to call this function.
	render_shaders_close(&c->shaders, vk);

	// Does NULL checking.
	comp_target_destroy(&c->target);

	if (vk->cmd_pool != VK_NULL_HANDLE) {
		vk->vkDestroyCommandPool(vk->device, vk->cmd_pool, NULL);
		vk->cmd_pool = VK_NULL_HANDLE;
	}

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

// clang-format off
#define COMP_INSTANCE_EXTENSIONS_COMMON                         \
	VK_EXT_DEBUG_REPORT_EXTENSION_NAME,                     \
	VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,      \
	VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,     \
	VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,  \
	VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, \
	VK_KHR_SURFACE_EXTENSION_NAME
// clang-format on

static const char *instance_extensions_common[] = {
    COMP_INSTANCE_EXTENSIONS_COMMON,
};

#ifdef VK_USE_PLATFORM_XCB_KHR
static const char *instance_extensions_xcb[] = {
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
};
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
static const char *instance_extensions_wayland[] = {
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
};

static const char *instance_extensions_direct_wayland[] = {
    VK_KHR_DISPLAY_EXTENSION_NAME,             //
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,     //
    VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME, //

#ifdef VK_EXT_acquire_drm_display
    VK_EXT_ACQUIRE_DRM_DISPLAY_EXTENSION_NAME,
#endif
};
#endif

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
static const char *instance_extensions_direct_mode[] = {
    VK_KHR_DISPLAY_EXTENSION_NAME,
    VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
    VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME,
};
#endif

#ifdef VK_USE_PLATFORM_DISPLAY_KHR
static const char *instance_extensions_vk_display[] = {
    VK_KHR_DISPLAY_EXTENSION_NAME,
};
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
static const char *instance_extensions_android[] = {
    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
};
#endif

#ifdef VK_USE_PLATFORM_WIN32_KHR
static const char *instance_extensions_windows[] = {
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};
#endif

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

static VkResult
select_instances_extensions(struct comp_compositor *c, struct u_string_list *required, struct u_string_list *optional)
{
	switch (c->settings.window_type) {
	case WINDOW_NONE: break;
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	case WINDOW_DIRECT_WAYLAND:
		u_string_list_append_array(required, instance_extensions_direct_wayland,
		                           ARRAY_SIZE(instance_extensions_direct_wayland));
		break;

	case WINDOW_WAYLAND:
		u_string_list_append_array(required, instance_extensions_wayland,
		                           ARRAY_SIZE(instance_extensions_wayland));
		break;
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
	case WINDOW_XCB:
		u_string_list_append_array(required, instance_extensions_xcb, ARRAY_SIZE(instance_extensions_xcb));
		break;
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
	case WINDOW_DIRECT_RANDR:
	case WINDOW_DIRECT_NVIDIA:
		u_string_list_append_array(required, instance_extensions_direct_mode,
		                           ARRAY_SIZE(instance_extensions_direct_mode));
		break;
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
	case WINDOW_ANDROID:
		u_string_list_append_array(required, instance_extensions_android,
		                           ARRAY_SIZE(instance_extensions_android));
		break;
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
	case WINDOW_MSWIN:
		u_string_list_append_array(required, instance_extensions_windows,
		                           ARRAY_SIZE(instance_extensions_windows));
		break;
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
	case WINDOW_VK_DISPLAY:
		u_string_list_append_array(required, instance_extensions_vk_display,
		                           ARRAY_SIZE(instance_extensions_vk_display));
		break;
#endif
	default: return VK_ERROR_INITIALIZATION_FAILED;
	}

#ifdef VK_EXT_display_surface_counter
	u_string_list_append(optional, VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME);
#endif

	return VK_SUCCESS;
}

static bool
compositor_init_vulkan(struct comp_compositor *c)
{
	struct vk_bundle *vk = get_vk(c);
	VkResult ret;

	// every backend needs at least the common extensions
	struct u_string_list *required_instance_ext_list =
	    u_string_list_create_from_array(instance_extensions_common, ARRAY_SIZE(instance_extensions_common));

	struct u_string_list *optional_instance_ext_list = u_string_list_create();

	ret = select_instances_extensions(c, required_instance_ext_list, optional_instance_ext_list);
	if (ret != VK_SUCCESS) {
		CVK_ERROR(c, "select_instances_extensions", "Failed to select instance extensions.", ret);
		u_string_list_destroy(&required_instance_ext_list);
		u_string_list_destroy(&optional_instance_ext_list);
		return ret;
	}

	struct u_string_list *required_device_extension_list =
	    u_string_list_create_from_array(required_device_extensions, ARRAY_SIZE(required_device_extensions));

	struct u_string_list *optional_device_extension_list =
	    u_string_list_create_from_array(optional_device_extensions, ARRAY_SIZE(optional_device_extensions));

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

	return true;
}


/*
 *
 * Other functions.
 *
 */

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
static bool
_match_allowlist_entry(const char *al_entry, VkDisplayPropertiesKHR *disp)
{
	unsigned long al_entry_length = strlen(al_entry);
	unsigned long disp_entry_length = strlen(disp->displayName);
	if (disp_entry_length < al_entry_length)
		return false;

	// we have a match with this allowlist entry.
	if (strncmp(al_entry, disp->displayName, al_entry_length) == 0)
		return true;

	return false;
}

/*
 * our physical device is an nvidia card, we can potentially select
 * nvidia-specific direct mode.
 *
 * we need to also check if we are confident that we can create a direct mode
 * display, if not we need to abandon the attempt here, and allow desktop-window
 * fallback to occur.
 */

static bool
_test_for_nvidia(struct comp_compositor *c, struct vk_bundle *vk)
{
	VkResult ret;

	VkPhysicalDeviceProperties physical_device_properties;
	vk->vkGetPhysicalDeviceProperties(vk->physical_device, &physical_device_properties);

	if (physical_device_properties.vendorID != 0x10DE)
		return false;

	// get a list of attached displays
	uint32_t display_count;

	ret = vk->vkGetPhysicalDeviceDisplayPropertiesKHR(vk->physical_device, &display_count, NULL);
	if (ret != VK_SUCCESS) {
		CVK_ERROR(c, "vkGetPhysicalDeviceDisplayPropertiesKHR", "Failed to get vulkan display count", ret);
		return false;
	}

	VkDisplayPropertiesKHR *display_props = U_TYPED_ARRAY_CALLOC(VkDisplayPropertiesKHR, display_count);

	if (display_props && vk->vkGetPhysicalDeviceDisplayPropertiesKHR(vk->physical_device, &display_count,
	                                                                 display_props) != VK_SUCCESS) {
		CVK_ERROR(c, "vkGetPhysicalDeviceDisplayPropertiesKHR", "Failed to get display properties", ret);
		free(display_props);
		return false;
	}

	for (uint32_t i = 0; i < display_count; i++) {
		VkDisplayPropertiesKHR *disp = display_props + i;
		// check this display against our allowlist
		for (uint32_t j = 0; j < ARRAY_SIZE(NV_DIRECT_ALLOWLIST); j++) {
			if (_match_allowlist_entry(NV_DIRECT_ALLOWLIST[j], disp)) {
				free(display_props);
				return true;
			}
		}

		if (c->settings.nvidia_display && _match_allowlist_entry(c->settings.nvidia_display, disp)) {
			free(display_props);
			return true;
		}
	}

	COMP_ERROR(c, "NVIDIA: No allowlisted displays found!");

	COMP_ERROR(c, "== Current Allowlist ==");
	for (uint32_t i = 0; i < ARRAY_SIZE(NV_DIRECT_ALLOWLIST); i++)
		COMP_ERROR(c, "%s", NV_DIRECT_ALLOWLIST[i]);

	COMP_ERROR(c, "== Found Displays ==");
	for (uint32_t i = 0; i < display_count; i++)
		COMP_ERROR(c, "%s", display_props[i].displayName);


	free(display_props);

	return false;
}
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT

static bool
compositor_check_vulkan_caps(struct comp_compositor *c)
{
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
	VkResult ret;

	// this is duplicative, but seems to be the easiest way to
	// 'pre-check' capabilities when window creation precedes vulkan
	// instance creation. we also need to load the VK_KHR_DISPLAY
	// extension.

	if (c->settings.window_type != WINDOW_AUTO) {
		COMP_DEBUG(c, "Skipping NVIDIA detection, window type forced.");
		return true;
	}
	COMP_DEBUG(c, "Checking for NVIDIA vulkan driver.");

	struct vk_bundle temp_vk_storage = {0};
	struct vk_bundle *temp_vk = &temp_vk_storage;
	temp_vk->log_level = U_LOGGING_WARN;

	ret = vk_get_loader_functions(temp_vk, vkGetInstanceProcAddr);
	if (ret != VK_SUCCESS) {
		CVK_ERROR(c, "vk_get_loader_functions", "Failed to get loader functions.", ret);
		return false;
	}

	const char *extension_names[] = {COMP_INSTANCE_EXTENSIONS_COMMON, VK_KHR_DISPLAY_EXTENSION_NAME};


	VkInstanceCreateInfo instance_create_info = {
	    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	    .enabledExtensionCount = ARRAY_SIZE(extension_names),
	    .ppEnabledExtensionNames = extension_names,
	};

	ret = temp_vk->vkCreateInstance(&instance_create_info, NULL, &(temp_vk->instance));
	if (ret != VK_SUCCESS) {
		CVK_ERROR(c, "vkCreateInstance", "Failed to create VkInstance.", ret);
		return false;
	}

	ret = vk_get_instance_functions(temp_vk);
	if (ret != VK_SUCCESS) {
		CVK_ERROR(c, "vk_get_instance_functions", "Failed to get Vulkan instance functions.", ret);
		return false;
	}

	bool use_compute = c->settings.use_compute;

	struct u_string_list *required_device_ext_list =
	    u_string_list_create_from_array(required_device_extensions, ARRAY_SIZE(required_device_extensions));

	struct u_string_list *optional_device_ext_list =
	    u_string_list_create_from_array(optional_device_extensions, ARRAY_SIZE(optional_device_extensions));

	// follow same device selection logic as subsequent calls
	ret = vk_create_device(                  //
	    temp_vk,                             //
	    c->settings.selected_gpu_index,      //
	    use_compute,                         // compute_only
	    VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_EXT, // global_priority
	    required_device_ext_list,            //
	    optional_device_ext_list,            //
	    NULL);                               // optional_device_features

	u_string_list_destroy(&required_device_ext_list);
	u_string_list_destroy(&optional_device_ext_list);

	if (ret != VK_SUCCESS) {
		CVK_ERROR(c, "vk_create_device", "Failed to create VkDevice.", ret);
		return false;
	}

	if (_test_for_nvidia(c, temp_vk)) {
		c->settings.window_type = WINDOW_DIRECT_NVIDIA;
		COMP_DEBUG(c, "Selecting direct NVIDIA window type!");
	}

	temp_vk->vkDestroyDevice(temp_vk->device, NULL);
	temp_vk->vkDestroyInstance(temp_vk->instance, NULL);

#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT
	return true;
}

static bool
compositor_try_window(struct comp_compositor *c, struct comp_target *ct)
{
	if (ct == NULL) {
		return false;
	}

	if (!comp_target_init_pre_vulkan(ct)) {
		ct->destroy(ct);
		return false;
	}

	COMP_DEBUG(c, "Window backend %s initialized!", ct->name);

	c->target = ct;

	return true;
}

static bool
compositor_init_window_pre_vulkan(struct comp_compositor *c)
{
	// Nothing to do for nvidia and vk_display.
	if (c->settings.window_type == WINDOW_DIRECT_NVIDIA || c->settings.window_type == WINDOW_VK_DISPLAY) {
		return true;
	}

	switch (c->settings.window_type) {
	case WINDOW_AUTO:
#if defined VK_USE_PLATFORM_WAYLAND_KHR && defined XRT_HAVE_WAYLAND_DIRECT
		if (compositor_try_window(c, comp_window_direct_wayland_create(c))) {
			c->settings.window_type = WINDOW_DIRECT_WAYLAND;
			return true;
		}
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
		if (compositor_try_window(c, comp_window_wayland_create(c))) {
			c->settings.window_type = WINDOW_WAYLAND;
			return true;
		}
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
		if (compositor_try_window(c, comp_window_direct_randr_create(c))) {
			c->settings.window_type = WINDOW_DIRECT_RANDR;
			return true;
		}
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
		if (compositor_try_window(c, comp_window_xcb_create(c))) {
			c->settings.window_type = WINDOW_XCB;
			COMP_DEBUG(c, "Using VK_PRESENT_MODE_IMMEDIATE_KHR for xcb window")
			c->settings.present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			return true;
		}
#endif
#ifdef XRT_OS_ANDROID
		if (compositor_try_window(c, comp_window_android_create(c))) {
			c->settings.window_type = WINDOW_ANDROID;
			return true;
		}
#endif
#ifdef XRT_OS_WINDOWS
		if (compositor_try_window(c, comp_window_mswin_create(c))) {
			c->settings.window_type = WINDOW_MSWIN;
			return true;
		}
#endif
		COMP_ERROR(c, "Failed to auto detect window support!");
		break;
	case WINDOW_XCB:
#ifdef VK_USE_PLATFORM_XCB_KHR
		compositor_try_window(c, comp_window_xcb_create(c));
		COMP_DEBUG(c, "Using VK_PRESENT_MODE_IMMEDIATE_KHR for xcb window")
		c->settings.present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
#else
		COMP_ERROR(c, "XCB support not compiled in!");
#endif
		break;
	case WINDOW_WAYLAND:
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
		compositor_try_window(c, comp_window_wayland_create(c));
#else
		COMP_ERROR(c, "Wayland support not compiled in!");
#endif
		break;
	case WINDOW_DIRECT_WAYLAND:
#if defined VK_USE_PLATFORM_WAYLAND_KHR && defined XRT_HAVE_WAYLAND_DIRECT
		compositor_try_window(c, comp_window_direct_wayland_create(c));
#else
		COMP_ERROR(c, "Wayland direct support not compiled in!");
#endif
		break;
	case WINDOW_DIRECT_RANDR:
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
		compositor_try_window(c, comp_window_direct_randr_create(c));
#else
		COMP_ERROR(c, "Direct mode support not compiled in!");
#endif
		break;
	case WINDOW_ANDROID:
#ifdef XRT_OS_ANDROID
		compositor_try_window(c, comp_window_android_create(c));
#else
		COMP_ERROR(c, "Android support not compiled in!");
#endif
		break;

	case WINDOW_MSWIN:
#ifdef XRT_OS_WINDOWS
		compositor_try_window(c, comp_window_mswin_create(c));
#else
		COMP_ERROR(c, "Windows support not compiled in!");
#endif
		break;
	default: COMP_ERROR(c, "Unknown window type!"); break;
	}

	// Failed to create?
	return c->target != NULL;
}

static bool
compositor_init_window_post_vulkan(struct comp_compositor *c)
{
	if (c->settings.window_type == WINDOW_DIRECT_NVIDIA) {
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
		return compositor_try_window(c, comp_window_direct_nvidia_create(c));
#else
		assert(false && "NVIDIA direct mode depends on the xlib/xrandr direct mode.");
		return false;
#endif
	}

	if (c->settings.window_type == WINDOW_VK_DISPLAY) {
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
		return compositor_try_window(c, comp_window_vk_display_create(c));
#else
		assert(false && "VkDisplayKHR direct mode depends on VK_USE_PLATFORM_DISPLAY_KHR.");
		return false;
#endif
	}

	return true;
}

static bool
compositor_init_swapchain(struct comp_compositor *c)
{
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
compositor_init_shaders(struct comp_compositor *c)
{
	struct vk_bundle *vk = get_vk(c);

	return render_shaders_load(&c->shaders, vk);
}

static bool
compositor_init_renderer(struct comp_compositor *c)
{
	if (!render_resources_init(&c->nr, &c->shaders, get_vk(c), c->xdev)) {
		return false;
	}

	c->r = comp_renderer_create(c);

#ifdef XRT_FEATURE_WINDOW_PEEK
	c->peek = comp_window_peek_create(c);
#else
	c->peek = NULL;
#endif

	return c->r != NULL;
}

xrt_result_t
xrt_gfx_provider_create_system(struct xrt_device *xdev, struct xrt_system_compositor **out_xsysc)
{
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
	    !compositor_check_vulkan_caps(c) ||
	    !compositor_init_window_pre_vulkan(c) ||
	    !compositor_init_vulkan(c) ||
	    !compositor_init_window_post_vulkan(c) ||
	    !compositor_init_shaders(c) ||
	    !compositor_init_swapchain(c) ||
	    !compositor_init_renderer(c)) {
		COMP_ERROR(c, "Failed to init compositor %p", (void *)c);
		c->base.base.base.destroy(&c->base.base.base);

		return XRT_ERROR_VULKAN;
	}
	// clang-format on

	comp_target_set_title(c->target, WINDOW_TITLE);

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



	comp_renderer_add_debug_vars(c->r);

	c->state = COMP_STATE_READY;

	// Standard app pacer.
	struct u_pacing_app_factory *upaf = NULL;
	xrt_result_t xret = u_pa_factory_create(&upaf);
	assert(xret == XRT_SUCCESS && upaf != NULL);

	return comp_multi_create_system_compositor(&c->base.base, upaf, sys_info, true, out_xsysc);
}
