// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  SDL compositor implementation.
 *
 * Based on src/xrt/compositor/null/null_compositor.c
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup sdl_test
 */

#include "xrt/xrt_gfx_native.h"

#include "os/os_time.h"

#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_verify.h"
#include "util/u_handles.h"
#include "util/u_trace_marker.h"

#include "util/comp_vulkan.h"

#include "multi/comp_multi_interface.h"

#include "sdl_internal.h"

#include <stdio.h>
#include <stdarg.h>


/*
 *
 * Helper functions.
 *
 */

static struct vk_bundle *
get_vk(struct sdl_compositor *c)
{
	return &c->base.vk;
}

#define SC_TRACE(c, ...) U_LOG_IFL_T(c->base.vk.log_level, __VA_ARGS__);
#define SC_DEBUG(c, ...) U_LOG_IFL_D(c->base.vk.log_level, __VA_ARGS__);
#define SC_INFO(c, ...) U_LOG_IFL_I(c->base.vk.log_level, __VA_ARGS__);
#define SC_WARN(c, ...) U_LOG_IFL_W(c->base.vk.log_level, __VA_ARGS__);
#define SC_ERROR(c, ...) U_LOG_IFL_E(c->base.vk.log_level, __VA_ARGS__);


/*
 *
 * Vulkan functions.
 *
 */

static const char *instance_extensions_common[] = {
    VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,      //
    VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,     //
    VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,  //
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, //
};

static const char *required_device_extensions[] = {
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
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME, //
    VK_KHR_EXTERNAL_FENCE_WIN32_EXTENSION_NAME,     //

#else
#error "Need port!"
#endif
};

static const char *optional_device_extensions[] = {
    VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME, //
    VK_EXT_GLOBAL_PRIORITY_EXTENSION_NAME,   //

// Platform version of "external_fence" and "external_semaphore"
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)      // Optional
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME, //
    VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,     //

#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE) // Not optional

#else
#error "Need port!"
#endif

#ifdef VK_KHR_image_format_list
    VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
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
};

static VkResult
select_instances_extensions(struct sdl_compositor *c, struct u_string_list *required, struct u_string_list *optional)
{
#ifdef VK_EXT_display_surface_counter
	u_string_list_append(optional, VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME);
#endif

	return VK_SUCCESS;
}

static bool
compositor_init_vulkan(struct sdl_compositor *c, enum u_logging_level log_level)
{
	struct vk_bundle *vk = get_vk(c);
	VkResult ret;

	// every backend needs at least the common extensions
	struct u_string_list *required_instance_ext_list =
	    u_string_list_create_from_array(instance_extensions_common, ARRAY_SIZE(instance_extensions_common));

	struct u_string_list *optional_instance_ext_list = u_string_list_create();

	ret = select_instances_extensions(c, required_instance_ext_list, optional_instance_ext_list);
	if (ret != VK_SUCCESS) {
		VK_ERROR(vk, "select_instances_extensions: %s\n\tFailed to select instance extensions.",
		         vk_result_string(ret));
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
	    .log_level = log_level,
	    .only_compute_queue = false, // Regular GFX
	    .selected_gpu_index = -1,    // Auto
	    .client_gpu_index = -1,      // Auto
	    .timeline_semaphore = true,  // Flag is optional, not a hard requirement.
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
	static_assert(ARRAY_SIZE(vk_res.client_gpu_deviceUUID.data) == ARRAY_SIZE(c->sys_info.client_vk_deviceUUID.data), "array size mismatch");
	static_assert(ARRAY_SIZE(vk_res.selected_gpu_deviceUUID.data) == ARRAY_SIZE(c->sys_info.compositor_vk_deviceUUID.data), "array size mismatch");
	static_assert(ARRAY_SIZE(vk_res.client_gpu_deviceLUID.data) == XRT_LUID_SIZE, "array size mismatch");
	static_assert(ARRAY_SIZE(vk_res.client_gpu_deviceLUID.data) == ARRAY_SIZE(c->sys_info.client_d3d_deviceLUID.data), "array size mismatch");
	// clang-format on

	c->sys_info.client_vk_deviceUUID = vk_res.client_gpu_deviceUUID;
	c->sys_info.compositor_vk_deviceUUID = vk_res.selected_gpu_deviceUUID;
	c->sys_info.client_d3d_deviceLUID = vk_res.client_gpu_deviceLUID;
	c->sys_info.client_d3d_deviceLUID_valid = vk_res.client_gpu_deviceLUID_valid;

	return true;
}


/*
 *
 * Other init functions.
 *
 */

static bool
compositor_init_pacing(struct sdl_compositor *c)
{
	xrt_result_t xret = u_pc_fake_create(c->settings.frame_interval_ns, os_monotonic_get_ns(), &c->upc);
	if (xret != XRT_SUCCESS) {
		SC_ERROR(c, "Failed to create fake pacing helper!");
		return false;
	}

	return true;
}

static bool
compositor_init_info(struct sdl_compositor *c)
{
	struct vk_bundle *vk = get_vk(c);
	struct xrt_compositor_info *info = &c->base.base.base.info;

	struct comp_vulkan_formats formats = {0};
	comp_vulkan_formats_check(vk, &formats);
	comp_vulkan_formats_copy_to_info(&formats, info);
	comp_vulkan_formats_log(c->base.vk.log_level, &formats);

	return true;
}

static bool
compositor_init_sys_info(struct sdl_compositor *c, struct sdl_program *sp, struct xrt_device *xdev)
{
	struct xrt_system_compositor_info *sys_info = &c->sys_info;

	// Required by OpenXR spec.
	sys_info->max_layers = 16;

	// UUIDs and LUID already set in vk init.
	(void)sys_info->compositor_vk_deviceUUID;
	(void)sys_info->client_vk_deviceUUID;
	(void)sys_info->client_d3d_deviceLUID;
	(void)sys_info->client_d3d_deviceLUID_valid;

	// Get window size and set recommended size to it.
	const int min = 128;
	const int max = 16 * 1024;
	int w = 0, h = 0;
	SDL_GetWindowSize(sp->win, &w, &h);
	if (w <= min || h <= min) {
		U_LOG_W("Window size is %ix%i which is smaller then %ix%i upping size.", w, h, min, min);
		w = min;
		h = min;
	}

	// clang-format off
	sys_info->views[0].recommended.width_pixels  = w;
	sys_info->views[0].recommended.height_pixels = h;
	sys_info->views[0].recommended.sample_count  = 1;
	sys_info->views[0].max.width_pixels          = max;
	sys_info->views[0].max.height_pixels         = max;
	sys_info->views[0].max.sample_count          = 1;

	sys_info->views[1].recommended.width_pixels  = min; // Second view is minimum
	sys_info->views[1].recommended.height_pixels = min; // Second view is minimum
	sys_info->views[1].recommended.sample_count  = 1;
	sys_info->views[1].max.width_pixels          = max;
	sys_info->views[1].max.height_pixels         = max;
	sys_info->views[1].max.sample_count          = 1;
	// clang-format on

	// Copy the list directly.
	assert(xdev->hmd->blend_mode_count <= XRT_MAX_DEVICE_BLEND_MODES);
	assert(xdev->hmd->blend_mode_count != 0);
	assert(xdev->hmd->blend_mode_count <= ARRAY_SIZE(sys_info->supported_blend_modes));
	for (size_t i = 0; i < xdev->hmd->blend_mode_count; ++i) {
		assert(u_verify_blend_mode_valid(xdev->hmd->blend_modes[i]));
		sys_info->supported_blend_modes[i] = xdev->hmd->blend_modes[i];
	}
	sys_info->supported_blend_mode_count = (uint8_t)xdev->hmd->blend_mode_count;

	// Refresh rates.
	sys_info->num_refresh_rates = 1;
	sys_info->refresh_rates[0] = (float)(1. / time_ns_to_s(c->settings.frame_interval_ns));

	return true;
}


/*
 *
 * Member functions.
 *
 */

static xrt_result_t
sdl_compositor_begin_session(struct xrt_compositor *xc, enum xrt_view_type type)
{
	struct sdl_compositor *c = &from_comp(xc)->c;
	SC_DEBUG(c, "BEGIN_SESSION");

	/*
	 * No logic needed here for the null compositor, if using the null
	 * compositor as a base for a new compositor put desired logic here.
	 */

	return XRT_SUCCESS;
}

static xrt_result_t
sdl_compositor_end_session(struct xrt_compositor *xc)
{
	struct sdl_compositor *c = &from_comp(xc)->c;
	SC_DEBUG(c, "END_SESSION");

	/*
	 * No logic needed here for the null compositor, if using the null
	 * compositor as a base for a new compositor put desired logic here.
	 */

	return XRT_SUCCESS;
}

static xrt_result_t
sdl_compositor_predict_frame(struct xrt_compositor *xc,
                             int64_t *out_frame_id,
                             uint64_t *out_wake_time_ns,
                             uint64_t *out_predicted_gpu_time_ns,
                             uint64_t *out_predicted_display_time_ns,
                             uint64_t *out_predicted_display_period_ns)
{
	COMP_TRACE_MARKER();

	struct sdl_compositor *c = &from_comp(xc)->c;

	SC_TRACE(c, "PREDICT_FRAME");

	uint64_t now_ns = os_monotonic_get_ns();
	uint64_t null_desired_present_time_ns = 0;
	uint64_t null_present_slop_ns = 0;
	uint64_t null_min_display_period_ns = 0;

	u_pc_predict(                        //
	    c->upc,                          // upc
	    now_ns,                          // now_ns
	    out_frame_id,                    // out_frame_id
	    out_wake_time_ns,                // out_wake_up_time_ns
	    &null_desired_present_time_ns,   // out_desired_present_time_ns
	    &null_present_slop_ns,           // out_present_slop_ns
	    out_predicted_display_time_ns,   // out_predicted_display_time_ns
	    out_predicted_display_period_ns, // out_predicted_display_period_ns
	    &null_min_display_period_ns);    // out_min_display_period_ns

	return XRT_SUCCESS;
}

static xrt_result_t
sdl_compositor_mark_frame(struct xrt_compositor *xc,
                          int64_t frame_id,
                          enum xrt_compositor_frame_point point,
                          uint64_t when_ns)
{
	COMP_TRACE_MARKER();

	struct sdl_compositor *c = &from_comp(xc)->c;

	SC_TRACE(c, "MARK_FRAME %i", point);

	switch (point) {
	case XRT_COMPOSITOR_FRAME_POINT_WOKE:
		u_pc_mark_point(c->upc, U_TIMING_POINT_WAKE_UP, frame_id, when_ns);
		return XRT_SUCCESS;
	default: assert(false);
	}

	return XRT_SUCCESS;
}

static xrt_result_t
sdl_compositor_begin_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct sdl_compositor *c = &from_comp(xc)->c;

	SC_TRACE(c, "BEGIN_FRAME");

	/*
	 * No logic needed here for the null compositor, if using the null
	 * compositor as a base for a new compositor put desired logic here.
	 */

	return XRT_SUCCESS;
}

static xrt_result_t
sdl_compositor_discard_frame(struct xrt_compositor *xc, int64_t frame_id)
{
	struct sdl_compositor *c = &from_comp(xc)->c;
	SC_TRACE(c, "DISCARD_FRAME");

	// Shouldn't be called.
	assert(false);

	return XRT_SUCCESS;
}

static xrt_result_t
sdl_compositor_layer_commit(struct xrt_compositor *xc, int64_t frame_id, xrt_graphics_sync_handle_t sync_handle)
{
	COMP_TRACE_MARKER();

	struct sdl_program *sp = from_comp(xc);
	struct sdl_compositor *c = &sp->c;

	SC_TRACE(c, "LAYER_COMMIT");

	/*
	 * The null compositor doesn't render and frames, but needs to do
	 * minimal bookkeeping and handling of arguments. If using the null
	 * compositor as a base for a new compositor this is where you render
	 * frames to be displayed to devices or remote clients.
	 */

	u_graphics_sync_unref(&sync_handle);

	/*
	 * Time keeping needed to keep the pacer happy.
	 */

	// When we begin rendering.
	{
		uint64_t now_ns = os_monotonic_get_ns();
		u_pc_mark_point(c->upc, U_TIMING_POINT_BEGIN, frame_id, now_ns);
	}

	// Render with SDL.
	sdl_program_plus_render(sp->spp);

	// When we are submitting to the GPU.
	{
		uint64_t now_ns = os_monotonic_get_ns();
		u_pc_mark_point(c->upc, U_TIMING_POINT_SUBMIT, frame_id, now_ns);
	}

	// Now is a good point to garbage collect.
	comp_swapchain_garbage_collect(&c->base.cscgc);

	return XRT_SUCCESS;
}

static xrt_result_t
sdl_compositor_poll_events(struct xrt_compositor *xc, union xrt_compositor_event *out_xce)
{
	struct sdl_compositor *c = &from_comp(xc)->c;

	SC_TRACE(c, "POLL_EVENTS");

	/*
	 * The null compositor does only minimal state keeping. If using the
	 * null compositor as a base for a new compositor this is where you can
	 * improve the state tracking. Note this is very often consumed only
	 * by the multi compositor.
	 */

	U_ZERO(out_xce);

	switch (c->state) {
	case SDL_COMP_STATE_UNINITIALIZED:
		SC_ERROR(c, "Polled uninitialized compositor");
		out_xce->state.type = XRT_COMPOSITOR_EVENT_NONE;
		break;
	case SDL_COMP_STATE_READY: out_xce->state.type = XRT_COMPOSITOR_EVENT_NONE; break;
	case SDL_COMP_STATE_PREPARED:
		SC_DEBUG(c, "PREPARED -> VISIBLE");
		out_xce->state.type = XRT_COMPOSITOR_EVENT_STATE_CHANGE;
		out_xce->state.visible = true;
		c->state = SDL_COMP_STATE_VISIBLE;
		break;
	case SDL_COMP_STATE_VISIBLE:
		SC_DEBUG(c, "VISIBLE -> FOCUSED");
		out_xce->state.type = XRT_COMPOSITOR_EVENT_STATE_CHANGE;
		out_xce->state.visible = true;
		out_xce->state.focused = true;
		c->state = SDL_COMP_STATE_FOCUSED;
		break;
	case SDL_COMP_STATE_FOCUSED:
		// No more transitions.
		out_xce->state.type = XRT_COMPOSITOR_EVENT_NONE;
		break;
	}

	return XRT_SUCCESS;
}

static void
sdl_compositor_destroy(struct xrt_compositor *xc)
{
	struct sdl_compositor *c = &from_comp(xc)->c;
	struct vk_bundle *vk = get_vk(c);

	SC_DEBUG(c, "DESTROY");

	// Make sure we don't have anything to destroy.
	comp_swapchain_garbage_collect(&c->base.cscgc);


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

	comp_base_fini(&c->base);

	u_pc_destroy(&c->upc);

	// Don't free as we are sub allocated.
}


/*
 *
 * 'Exported' functions.
 *
 */

void
sdl_compositor_init(struct sdl_program *sp)
{
	struct xrt_device *xdev = &sp->xdev_base;
	enum u_logging_level log_level = sp->log_level;

	struct sdl_compositor *c = &sp->c;

	c->base.base.base.begin_session = sdl_compositor_begin_session;
	c->base.base.base.end_session = sdl_compositor_end_session;
	c->base.base.base.predict_frame = sdl_compositor_predict_frame;
	c->base.base.base.mark_frame = sdl_compositor_mark_frame;
	c->base.base.base.begin_frame = sdl_compositor_begin_frame;
	c->base.base.base.discard_frame = sdl_compositor_discard_frame;
	c->base.base.base.layer_commit = sdl_compositor_layer_commit;
	c->base.base.base.poll_events = sdl_compositor_poll_events;
	c->base.base.base.destroy = sdl_compositor_destroy;
	c->base.vk.log_level = log_level;
	c->frame.waited.id = -1;
	c->frame.rendering.id = -1;
	c->state = SDL_COMP_STATE_READY;
	c->settings.frame_interval_ns = U_TIME_1S_IN_NS / 20; // 20 FPS

	SC_DEBUG(c, "Doing init %p", (void *)c);

	// Do this as early as possible
	comp_base_init(&c->base);

	// Override some comp_base functions.
	c->base.base.base.create_swapchain = sdl_swapchain_create;
	c->base.base.base.import_swapchain = sdl_swapchain_import;


	/*
	 * Main init sequence.
	 */

	if (!compositor_init_pacing(c) ||             //
	    !compositor_init_vulkan(c, log_level) ||  //
	    !compositor_init_sys_info(c, sp, xdev) || //
	    !compositor_init_info(c)) {               //
		SC_DEBUG(c, "Failed to init compositor %p", (void *)c);
		c->base.base.base.destroy(&c->base.base.base);

		assert(false);
	}

	SC_DEBUG(c, "Done %p", (void *)c);
}

xrt_result_t
sdl_compositor_create_system(struct sdl_program *sp, struct xrt_system_compositor **out_xsysc)
{
	// Standard app pacer.
	struct u_pacing_app_factory *upaf = NULL;
	xrt_result_t xret = u_pa_factory_create(&upaf);
	assert(xret == XRT_SUCCESS && upaf != NULL);

	return comp_multi_create_system_compositor(&sp->c.base.base, upaf, &sp->c.sys_info, false, out_xsysc);
}
