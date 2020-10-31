// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main compositor written using Vulkan implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
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

#include "os/os_time.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_distortion_mesh.h"

#include "main/comp_compositor.h"

#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_TITLE "Monado"

/*!
 */
static void
compositor_destroy(struct xrt_compositor *xc)
{
	struct comp_compositor *c = comp_compositor(xc);
	struct vk_bundle *vk = &c->vk;

	COMP_DEBUG(c, "DESTROY");

	// Make sure we don't have anything to destroy.
	comp_compositor_garbage_collect(c);

	if (c->r) {
		comp_renderer_destroy(c->r);
		c->r = NULL;
	}

	comp_resources_close(c, &c->nr);

	// As long as vk_bundle is valid it's safe to call this function.
	comp_shaders_close(&c->vk, &c->shaders);

	if (c->window != NULL) {
		vk_swapchain_cleanup(&c->window->swapchain);
		c->window->destroy(c->window);
		c->window = NULL;
	}

	if (vk->cmd_pool != VK_NULL_HANDLE) {
		vk->vkDestroyCommandPool(vk->device, vk->cmd_pool, NULL);
		vk->cmd_pool = VK_NULL_HANDLE;
	}

	if (vk->device != VK_NULL_HANDLE) {
		vk->vkDestroyDevice(vk->device, NULL);
		vk->device = VK_NULL_HANDLE;
	}

	if (vk->instance != VK_NULL_HANDLE) {
		vk->vkDestroyInstance(vk->instance, NULL);
		vk->instance = VK_NULL_HANDLE;
	}

	if (c->compositor_frame_times.debug_var) {
		free(c->compositor_frame_times.debug_var);
	}

	u_threading_stack_fini(&c->threading.destroy_swapchains);

	free(c);
}

static xrt_result_t
compositor_prepare_session(struct xrt_compositor *xc,
                           const struct xrt_session_prepare_info *xspi)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_DEBUG(c, "PREPARE_SESSION");

	c->state = COMP_STATE_PREPARED;

	return XRT_SUCCESS;
}

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

/*!
 * @brief Utility for waiting (for rendering purposes) until the next vsync or a
 * specified time point, whichever comes first.
 *
 * Only for rendering - this will busy-wait if needed.
 *
 * @return true if we waited until the time indicated
 *
 * @todo In the future, this may differ between platforms since some have ways
 * to directly wait on a vsync.
 */
static bool
compositor_wait_vsync_or_time(struct comp_compositor *c, int64_t wake_up_time)
{

	int64_t now_ns = os_monotonic_get_ns();
	/*!
	 * @todo this is not accurate, but it serves the purpose of not letting
	 * us sleep longer than the next vsync usually
	 */
	int64_t next_vsync = now_ns + c->settings.nominal_frame_interval_ns / 2;

	bool ret = true;
	// Sleep until the sooner of vsync or our deadline.
	if (next_vsync < wake_up_time) {
		ret = false;
		wake_up_time = next_vsync;
	}
	int64_t wait_duration = wake_up_time - now_ns;
	if (wait_duration <= 0) {
		// Don't wait at all
		return ret;
	}

	if (wait_duration > 1000000) {
		os_nanosleep(wait_duration - (wait_duration % 1000000));
	}
	// Busy-wait for fine-grained delays.
	while (now_ns < wake_up_time) {
		now_ns = os_monotonic_get_ns();
	}

	return ret;
}

static xrt_result_t
compositor_wait_frame(struct xrt_compositor *xc,
                      int64_t *out_frame_id,
                      uint64_t *predicted_display_time,
                      uint64_t *predicted_display_period)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_SPEW(c, "WAIT_FRAME");

	// A little bit easier to read.
	int64_t interval_ns = (int64_t)c->settings.nominal_frame_interval_ns;

	int64_t now_ns = os_monotonic_get_ns();
	if (c->last_next_display_time == 0) {
		// First frame, we'll just assume we will display immediately

		*predicted_display_period = interval_ns;
		c->last_next_display_time = now_ns + interval_ns;
		*predicted_display_time = c->last_next_display_time;
		*out_frame_id = c->last_next_display_time;

		return XRT_SUCCESS;
	}

	// First estimate of next display time.
	while (1) {

		int64_t render_time_ns =
		    c->expected_app_duration_ns + c->frame_overhead_ns;
		int64_t swap_interval =
		    ceilf((float)render_time_ns / interval_ns);
		int64_t render_interval_ns = swap_interval * interval_ns;
		int64_t next_display_time =
		    c->last_next_display_time + render_interval_ns;
		/*!
		 * @todo adjust next_display_time to be a multiple of
		 * interval_ns from c->last_frame_time_ns
		 */

		while ((next_display_time - render_time_ns) < now_ns) {
			// we can't unblock in the past
			next_display_time += render_interval_ns;
		}
		if (compositor_wait_vsync_or_time(
		        c, (next_display_time - render_time_ns))) {
			// True return val means we actually waited for the
			// deadline.
			*predicted_display_period =
			    next_display_time - c->last_next_display_time;
			*predicted_display_time = next_display_time;
			*out_frame_id = c->last_next_display_time;

			c->last_next_display_time = next_display_time;
			return XRT_SUCCESS;
		}
	}
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
	COMP_SPEW(c, "DISCARD_FRAME");
	return XRT_SUCCESS;
}

static void
compositor_add_frame_timing(struct comp_compositor *c)
{
	int last_index = c->compositor_frame_times.index;

	c->compositor_frame_times.index++;
	c->compositor_frame_times.index %= NUM_FRAME_TIMES;

	// update fps only once every FPS_NUM_TIMINGS
	if (c->compositor_frame_times.index == 0) {
		float total_s = 0;

		// frame *timings* are durations between *times*
		int NUM_FRAME_TIMINGS = NUM_FRAME_TIMES - 1;

		for (int i = 0; i < NUM_FRAME_TIMINGS; i++) {
			uint64_t frametime_ns =
			    c->compositor_frame_times.times_ns[i + 1] -
			    c->compositor_frame_times.times_ns[i];
			float frametime_s =
			    frametime_ns * 1. / 1000. * 1. / 1000. * 1. / 1000.;
			total_s += frametime_s;
		}
		float avg_frametime_s = total_s / ((float)NUM_FRAME_TIMINGS);
		c->compositor_frame_times.fps = 1. / avg_frametime_s;
	}

	c->compositor_frame_times.times_ns[c->compositor_frame_times.index] =
	    os_monotonic_get_ns();

	uint64_t diff = c->compositor_frame_times
	                    .times_ns[c->compositor_frame_times.index] -
	                c->compositor_frame_times.times_ns[last_index];
	c->compositor_frame_times.timings_ms[c->compositor_frame_times.index] =
	    (float)diff * 1. / 1000. * 1. / 1000.;
}

static xrt_result_t
compositor_layer_begin(struct xrt_compositor *xc,
                       int64_t frame_id,
                       enum xrt_blend_mode env_blend_mode)
{
	struct comp_compositor *c = comp_compositor(xc);

	// Always zero for now.
	uint32_t slot_id = 0;

	c->slots[slot_id].env_blend_mode = env_blend_mode;
	c->slots[slot_id].num_layers = 0;
	return XRT_SUCCESS;
}

static xrt_result_t
compositor_layer_stereo_projection(struct xrt_compositor *xc,
                                   struct xrt_device *xdev,
                                   struct xrt_swapchain *l_xsc,
                                   struct xrt_swapchain *r_xsc,
                                   const struct xrt_layer_data *data)
{
	struct comp_compositor *c = comp_compositor(xc);

	// Without IPC we only have one slot
	uint32_t slot_id = 0;
	uint32_t layer_id = c->slots[slot_id].num_layers;

	struct comp_layer *layer = &c->slots[slot_id].layers[layer_id];
	layer->scs[0] = comp_swapchain(l_xsc);
	layer->scs[1] = comp_swapchain(r_xsc);
	layer->data = *data;

	c->slots[slot_id].num_layers++;
	return XRT_SUCCESS;
}

static xrt_result_t
compositor_layer_stereo_projection_depth(struct xrt_compositor *xc,
                                         struct xrt_device *xdev,
                                         struct xrt_swapchain *l_xsc,
                                         struct xrt_swapchain *r_xsc,
                                         struct xrt_swapchain *l_d_xsc,
                                         struct xrt_swapchain *r_d_xsc,
                                         const struct xrt_layer_data *data)
{
	struct comp_compositor *c = comp_compositor(xc);

	// Without IPC we only have one slot
	uint32_t slot_id = 0;
	uint32_t layer_id = c->slots[slot_id].num_layers;

	struct comp_layer *layer = &c->slots[slot_id].layers[layer_id];
	layer->scs[0] = comp_swapchain(l_xsc);
	layer->scs[1] = comp_swapchain(r_xsc);
	layer->data = *data;

	c->slots[slot_id].num_layers++;
	return XRT_SUCCESS;
}

static xrt_result_t
do_single(struct xrt_compositor *xc,
          struct xrt_device *xdev,
          struct xrt_swapchain *xsc,
          const struct xrt_layer_data *data)
{
	struct comp_compositor *c = comp_compositor(xc);

	// Without IPC we only have one slot
	uint32_t slot_id = 0;
	uint32_t layer_id = c->slots[slot_id].num_layers;

	struct comp_layer *layer = &c->slots[slot_id].layers[layer_id];
	layer->scs[0] = comp_swapchain(xsc);
	layer->scs[1] = NULL;
	layer->data = *data;

	c->slots[slot_id].num_layers++;
	return XRT_SUCCESS;
}

static xrt_result_t
compositor_layer_quad(struct xrt_compositor *xc,
                      struct xrt_device *xdev,
                      struct xrt_swapchain *xsc,
                      const struct xrt_layer_data *data)
{
	return do_single(xc, xdev, xsc, data);
}

static xrt_result_t
compositor_layer_cube(struct xrt_compositor *xc,
                      struct xrt_device *xdev,
                      struct xrt_swapchain *xsc,
                      const struct xrt_layer_data *data)
{
#if 0
	return do_single(xc, xdev, xsc, data);
#else
	return XRT_SUCCESS; //! @todo Implement
#endif
}

static xrt_result_t
compositor_layer_cylinder(struct xrt_compositor *xc,
                          struct xrt_device *xdev,
                          struct xrt_swapchain *xsc,
                          const struct xrt_layer_data *data)
{
	return do_single(xc, xdev, xsc, data);
}

static xrt_result_t
compositor_layer_equirect(struct xrt_compositor *xc,
                          struct xrt_device *xdev,
                          struct xrt_swapchain *xsc,
                          const struct xrt_layer_data *data)
{
	return do_single(xc, xdev, xsc, data);
}

static xrt_result_t
compositor_layer_commit(struct xrt_compositor *xc, int64_t frame_id)
{
	struct comp_compositor *c = comp_compositor(xc);

	COMP_SPEW(c, "LAYER_COMMIT");

	// Always zero for now.
	uint32_t slot_id = 0;
	uint32_t num_layers = c->slots[slot_id].num_layers;

	comp_renderer_destroy_layers(c->r);
	comp_renderer_allocate_layers(c->r, num_layers);

	for (uint32_t i = 0; i < num_layers; i++) {
		struct comp_layer *layer = &c->slots[slot_id].layers[i];
		struct xrt_layer_data *data = &layer->data;
		switch (data->type) {
		case XRT_LAYER_QUAD: {
			struct xrt_layer_quad_data *quad = &layer->data.quad;
			struct comp_swapchain_image *image;
			image = &layer->scs[0]->images[quad->sub.image_index];
			comp_renderer_set_quad_layer(c->r, i, image, data);
		} break;
		case XRT_LAYER_STEREO_PROJECTION: {
			struct xrt_layer_stereo_projection_data *stereo =
			    &data->stereo;
			struct comp_swapchain_image *right;
			struct comp_swapchain_image *left;
			left =
			    &layer->scs[0]->images[stereo->l.sub.image_index];
			right =
			    &layer->scs[1]->images[stereo->r.sub.image_index];

			comp_renderer_set_projection_layer(c->r, i, left, right,
			                                   data);
		} break;
		case XRT_LAYER_STEREO_PROJECTION_DEPTH: {
			struct xrt_layer_stereo_projection_depth_data *stereo =
			    &data->stereo_depth;
			struct comp_swapchain_image *right;
			struct comp_swapchain_image *left;
			left =
			    &layer->scs[0]->images[stereo->l.sub.image_index];
			right =
			    &layer->scs[1]->images[stereo->r.sub.image_index];

			//! @todo: Make use of stereo->l_d and stereo->r_d

			comp_renderer_set_projection_layer(c->r, i, left, right,
			                                   data);
		} break;
		case XRT_LAYER_CYLINDER: {
			struct xrt_layer_cylinder_data *cyl =
			    &layer->data.cylinder;
			struct comp_swapchain_image *image;
			image = &layer->scs[0]->images[cyl->sub.image_index];
			comp_renderer_set_cylinder_layer(c->r, i, image, data);
		} break;
		case XRT_LAYER_EQUIRECT: {
			struct xrt_layer_equirect_data *eq =
			    &layer->data.equirect;
			struct comp_swapchain_image *image;
			image = &layer->scs[0]->images[eq->sub.image_index];
			comp_renderer_set_equirect_layer(c->r, i, image, data);
		} break;
		case XRT_LAYER_CUBE:
			// Should never end up here.
			assert(false);
		}
	}

	comp_renderer_draw(c->r);

	compositor_add_frame_timing(c);

	// Record the time of this frame.
	c->last_frame_time_ns = os_monotonic_get_ns();
	c->app_profiling.last_end = c->last_frame_time_ns;

	//! @todo do a time-weighted average or something.
	c->expected_app_duration_ns =
	    c->app_profiling.last_end - c->app_profiling.last_begin;

	if (c->state == COMP_STATE_PREPARED) {
		c->state = COMP_STATE_COMMITTED;
	}

	// Now is a good point to garbage collect.
	comp_compositor_garbage_collect(c);
	return XRT_SUCCESS;
}

static xrt_result_t
compositor_poll_events(struct xrt_compositor *xc,
                       union xrt_compositor_event *out_xce)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_SPEW(c, "POLL_EVENTS");

	U_ZERO(out_xce);

	switch (c->state) {
	case COMP_STATE_READY:
		out_xce->state.type = XRT_COMPOSITOR_EVENT_NONE;
		break;
	case COMP_STATE_PREPARED:
		out_xce->state.type = XRT_COMPOSITOR_EVENT_NONE;
		break;
	case COMP_STATE_COMMITTED:
		COMP_DEBUG(c, "COMMITTED -> VISIBLE");
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


/*
 *
 * xdev functions.
 *
 */

static bool
compositor_check_and_prepare_xdev(struct comp_compositor *c,
                                  struct xrt_device *xdev)
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
		COMP_ERROR(
		    c, "The xdev '%s' didn't have none nor compute distortion.",
		    xdev->str);
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

#define GET_DEV_PROC(c, name)                                                  \
	(PFN_##name) c->vk.vkGetDeviceProcAddr(c->vk.device, #name);
#define GET_INS_PROC(c, name)                                                  \
	(PFN_##name) c->vk.vkGetInstanceProcAddr(c->vk.instance, #name);
#define GET_DEV_PROC(c, name)                                                  \
	(PFN_##name) c->vk.vkGetDeviceProcAddr(c->vk.device, #name);

// NOLINTNEXTLINE // don't remove the forward decl.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName);

static VkResult
find_get_instance_proc_addr(struct comp_compositor *c)
{
	//! @todo Do any library loading here.
	return vk_get_loader_functions(&c->vk, vkGetInstanceProcAddr);
}

#define COMPOSITOR_COMMON_VULKAN_EXTENSIONS                                    \
	VK_KHR_SURFACE_EXTENSION_NAME,                                         \
	    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,            \
	    VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,                \
	    VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,                 \
	    VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME

static const char *instance_extensions_none[] = {
    COMPOSITOR_COMMON_VULKAN_EXTENSIONS};

#ifdef VK_USE_PLATFORM_XCB_KHR
static const char *instance_extensions_xcb[] = {
    COMPOSITOR_COMMON_VULKAN_EXTENSIONS,
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
};
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
static const char *instance_extensions_wayland[] = {
    COMPOSITOR_COMMON_VULKAN_EXTENSIONS,
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
};
#endif

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
static const char *instance_extensions_direct_mode[] = {
    COMPOSITOR_COMMON_VULKAN_EXTENSIONS,
    VK_KHR_DISPLAY_EXTENSION_NAME,
    VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
    VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME,
};
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
static const char *instance_extensions_android[] = {
    COMPOSITOR_COMMON_VULKAN_EXTENSIONS,
    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
};
#endif

static VkResult
select_instances_extensions(struct comp_compositor *c,
                            const char ***out_exts,
                            uint32_t *out_num)
{
	switch (c->settings.window_type) {
	case WINDOW_NONE:
		*out_exts = instance_extensions_none;
		*out_num = ARRAY_SIZE(instance_extensions_none);
		break;
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	case WINDOW_WAYLAND:
		*out_exts = instance_extensions_wayland;
		*out_num = ARRAY_SIZE(instance_extensions_wayland);
		break;
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
	case WINDOW_XCB:
		*out_exts = instance_extensions_xcb;
		*out_num = ARRAY_SIZE(instance_extensions_xcb);
		break;
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
	case WINDOW_DIRECT_RANDR:
	case WINDOW_DIRECT_NVIDIA:
		*out_exts = instance_extensions_direct_mode;
		*out_num = ARRAY_SIZE(instance_extensions_direct_mode);
		break;
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
	case WINDOW_ANDROID:
		*out_exts = instance_extensions_android;
		*out_num = ARRAY_SIZE(instance_extensions_android);
		break;
#endif
	default: return VK_ERROR_INITIALIZATION_FAILED;
	}

	return VK_SUCCESS;
}

static VkResult
create_instance(struct comp_compositor *c)
{
	const char **instance_extensions;
	uint32_t num_extensions;
	VkResult ret;

	VkApplicationInfo app_info = {
	    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	    .pApplicationName = "Collabora Compositor",
	    .pEngineName = "Monado",
	    .apiVersion = VK_MAKE_VERSION(1, 0, 2),
	};

	ret = select_instances_extensions(c, &instance_extensions,
	                                  &num_extensions);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "Failed to select instance extensions: %s",
		           vk_result_string(ret));
		return ret;
	}

	VkInstanceCreateInfo instance_info = {
	    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	    .pApplicationInfo = &app_info,
	    .enabledExtensionCount = num_extensions,
	    .ppEnabledExtensionNames = instance_extensions,
	};

	ret = c->vk.vkCreateInstance(&instance_info, NULL, &c->vk.instance);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "vkCreateInstance: %s\n", vk_result_string(ret));
		COMP_ERROR(c, "Failed to create Vulkan instance");
		return ret;
	}

	ret = vk_get_instance_functions(&c->vk);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "Failed to get Vulkan instance functions: %s",
		           vk_result_string(ret));
		return ret;
	}

	return ret;
}

static bool
get_device_uuid(struct vk_bundle *vk,
                struct comp_compositor *c,
                int gpu_index,
                uint8_t *uuid)
{
	VkPhysicalDeviceIDProperties pdidp = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};

	VkPhysicalDeviceProperties2 pdp2 = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
	    .pNext = &pdidp};

	VkPhysicalDevice phys[16];
	uint32_t gpu_count = ARRAY_SIZE(phys);
	VkResult ret;

	ret = vk->vkEnumeratePhysicalDevices(vk->instance, &gpu_count, phys);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "Failed to enumerate physical devices!");
		return false;
	}
	vk->vkGetPhysicalDeviceProperties2(phys[gpu_index], &pdp2);
	memcpy(uuid, pdidp.deviceUUID, XRT_GPU_UUID_SIZE);

	return true;
}

static bool
compositor_init_vulkan(struct comp_compositor *c)
{

	VkResult ret;

	c->vk.print = c->settings.log_level <= U_LOGGING_DEBUG;

	ret = find_get_instance_proc_addr(c);
	if (ret != VK_SUCCESS) {
		return false;
	}

	ret = create_instance(c);
	if (ret != VK_SUCCESS) {
		return false;
	}

	ret = vk_create_device(&c->vk, c->settings.selected_gpu_index);
	if (ret != VK_SUCCESS) {
		return false;
	}
	c->settings.selected_gpu_index = c->vk.physical_device_index;

	// store physical device UUID for compositor in settings
	if (c->settings.selected_gpu_index >= 0) {
		if (get_device_uuid(&c->vk, c, c->settings.selected_gpu_index,
		                    c->settings.selected_gpu_deviceUUID)) {
			char uuid_str[XRT_GPU_UUID_SIZE * 3 + 1] = {0};
			for (int i = 0; i < XRT_GPU_UUID_SIZE; i++) {
				sprintf(uuid_str + i * 3, "%02x ",
				        c->settings.selected_gpu_deviceUUID[i]);
			}
			COMP_DEBUG(c, "Selected %d with uuid: %s",
			           c->settings.selected_gpu_index, uuid_str);
		} else {
			COMP_ERROR(c, "Failed to get device %d uuid",
			           c->settings.selected_gpu_index);
		}
	}

	// by default suggest GPU used by compositor to clients
	if (c->settings.client_gpu_index < 0) {
		c->settings.client_gpu_index = c->settings.selected_gpu_index;
	}

	// store physical device UUID suggested to clients in settings
	if (c->settings.client_gpu_index >= 0) {
		if (get_device_uuid(&c->vk, c, c->settings.client_gpu_index,
		                    c->settings.client_gpu_deviceUUID)) {
			char uuid_str[XRT_GPU_UUID_SIZE * 3 + 1] = {0};
			for (int i = 0; i < XRT_GPU_UUID_SIZE; i++) {
				sprintf(uuid_str + i * 3, "%02x ",
				        c->settings.client_gpu_deviceUUID[i]);
			}
			COMP_DEBUG(c, "Suggest %d with uuid: %s to clients",
			           c->settings.client_gpu_index, uuid_str);
		} else {
			COMP_ERROR(c, "Failed to get device %d uuid",
			           c->settings.client_gpu_index);
		}
	}

	ret = vk_init_cmd_pool(&c->vk);
	return ret == VK_SUCCESS;
}


/*
 *
 * Other functions.
 *
 */

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
static bool
_match_wl_entry(const char *wl_entry, VkDisplayPropertiesKHR *disp)
{
	unsigned long wl_entry_length = strlen(wl_entry);
	unsigned long disp_entry_length = strlen(disp->displayName);
	if (disp_entry_length < wl_entry_length)
		return false;

	// we have a match with this whitelist entry.
	if (strncmp(wl_entry, disp->displayName, wl_entry_length) == 0)
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
	VkPhysicalDeviceProperties physical_device_properties;
	vk->vkGetPhysicalDeviceProperties(vk->physical_device,
	                                  &physical_device_properties);

	if (physical_device_properties.vendorID != 0x10DE)
		return false;

	// get a list of attached displays
	uint32_t display_count;

	if (vk->vkGetPhysicalDeviceDisplayPropertiesKHR(
	        vk->physical_device, &display_count, NULL) != VK_SUCCESS) {
		COMP_ERROR(c, "Failed to get vulkan display count");
		return false;
	}

	VkDisplayPropertiesKHR *display_props =
	    U_TYPED_ARRAY_CALLOC(VkDisplayPropertiesKHR, display_count);

	if (display_props && vk->vkGetPhysicalDeviceDisplayPropertiesKHR(
	                         vk->physical_device, &display_count,
	                         display_props) != VK_SUCCESS) {
		COMP_ERROR(c, "Failed to get display properties");
		free(display_props);
		return false;
	}

	for (uint32_t i = 0; i < display_count; i++) {
		VkDisplayPropertiesKHR *disp = display_props + i;
		// check this display against our whitelist
		for (uint32_t j = 0; j < ARRAY_SIZE(NV_DIRECT_WHITELIST); j++) {
			if (_match_wl_entry(NV_DIRECT_WHITELIST[j], disp)) {
				free(display_props);
				return true;
			}
		}

		if (c->settings.nvidia_display &&
		    _match_wl_entry(c->settings.nvidia_display, disp)) {
			free(display_props);
			return true;
		}
	}

	COMP_ERROR(c, "NVIDIA: No whitelisted displays found!");

	COMP_ERROR(c, "== Current Whitelist ==");
	for (uint32_t i = 0; i < ARRAY_SIZE(NV_DIRECT_WHITELIST); i++)
		COMP_ERROR(c, "%s", NV_DIRECT_WHITELIST[i]);

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

	struct vk_bundle temp_vk = {0};
	ret = vk_get_loader_functions(&temp_vk, vkGetInstanceProcAddr);
	if (ret != VK_SUCCESS) {
		return false;
	}

	const char *extension_names[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
		VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
		VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
#if !defined(XRT_OS_ANDROID)
		VK_KHR_DISPLAY_EXTENSION_NAME,
#endif
	};

	VkInstanceCreateInfo instance_create_info = {
	    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	    .enabledExtensionCount = ARRAY_SIZE(extension_names),
	    .ppEnabledExtensionNames = extension_names,
	};

	ret = temp_vk.vkCreateInstance(&instance_create_info, NULL,
	                               &(temp_vk.instance));
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "Failed to create VkInstance: %s",
		           vk_result_string(ret));
		return false;
	}

	ret = vk_get_instance_functions(&temp_vk);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "Failed to get Vulkan instance functions: %s",
		           vk_result_string(ret));
		return false;
	}

	// follow same device selection logic as subsequent calls
	ret = vk_create_device(&temp_vk, c->settings.selected_gpu_index);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "Failed to create VkDevice: %s",
		           vk_result_string(ret));
		return false;
	}

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
	if (_test_for_nvidia(c, &temp_vk)) {
		c->settings.window_type = WINDOW_DIRECT_NVIDIA;
		COMP_DEBUG(c, "Selecting direct NVIDIA window type!");
	}
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT

	temp_vk.vkDestroyDevice(temp_vk.device, NULL);
	temp_vk.vkDestroyInstance(temp_vk.instance, NULL);

	return true;
}

static bool
compositor_try_window(struct comp_compositor *c, struct comp_window *window)
{
	if (window == NULL) {
		return false;
	}

	if (!window->init(window)) {
		window->destroy(window);
		return false;
	}
	COMP_DEBUG(c, "Window backend %s initialized!", window->name);
	c->window = window;
	return true;
}

static bool
compositor_init_window_pre_vulkan(struct comp_compositor *c)
{
	// Setup the initial width from the settings.
	c->current.width = c->settings.width;
	c->current.height = c->settings.height;

	// Nothing to do for nvidia.
	if (c->settings.window_type == WINDOW_DIRECT_NVIDIA) {
		return true;
	}

	switch (c->settings.window_type) {
	case WINDOW_AUTO:
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
		if (compositor_try_window(c, comp_window_wayland_create(c))) {
			c->settings.window_type = WINDOW_WAYLAND;
			return true;
		}
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
		if (compositor_try_window(c,
		                          comp_window_direct_randr_create(c))) {
			c->settings.window_type = WINDOW_DIRECT_RANDR;
			return true;
		}
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
		if (compositor_try_window(c, comp_window_xcb_create(c))) {
			c->settings.window_type = WINDOW_XCB;
			return true;
		}
#endif
#ifdef XRT_OS_ANDROID
		if (compositor_try_window(c, comp_window_android_create(c))) {
			c->settings.window_type = WINDOW_ANDROID;
			return true;
		}
#endif
		COMP_ERROR(c, "Failed to auto detect window support!");
		break;
	case WINDOW_XCB:
#ifdef VK_USE_PLATFORM_XCB_KHR
		compositor_try_window(c, comp_window_xcb_create(c));
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
	default: COMP_ERROR(c, "Unknown window type!"); break;
	}

	// Failed to create?
	return c->window != NULL;
}

static bool
compositor_init_window_post_vulkan(struct comp_compositor *c)
{
	if (c->settings.window_type != WINDOW_DIRECT_NVIDIA) {
		return true;
	}

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
	return compositor_try_window(c, comp_window_direct_nvidia_create(c));
#else
	assert(false &&
	       "NVIDIA direct mode depends on the xlib/xrandr direct mode.");
	return false;
#endif
}

static void
_sc_dimension_cb(uint32_t width, uint32_t height, void *ptr)
{
	struct comp_compositor *c = (struct comp_compositor *)ptr;

	COMP_DEBUG(c, "_sc_dimension_cb %dx%d", width, height);

	c->current.width = width;
	c->current.height = height;
}

static bool
compositor_init_swapchain(struct comp_compositor *c)
{
	//! @todo Make c->window->init_swachain call vk_swapchain_init
	//! and give
	//!       _sc_dimension_cb to window or just have it call a
	//!       function?

	vk_swapchain_init(&c->window->swapchain, &c->vk, _sc_dimension_cb,
	                  (void *)c);
	if (!c->window->init_swapchain(c->window, c->current.width,
	                               c->current.height)) {
		COMP_ERROR(c, "Window init_swapchain failed!");
		goto err_destroy;
	}

	return true;

	// Error path.
err_destroy:
	c->window->destroy(c->window);
	c->window = NULL;

	return false;
}

static bool
compositor_init_shaders(struct comp_compositor *c)
{
	return comp_shaders_load(&c->vk, &c->shaders);
}

static bool
compositor_init_renderer(struct comp_compositor *c)
{
	if (!comp_resources_init(c, &c->nr)) {
		return false;
	}

	c->r = comp_renderer_create(c);
	return c->r != NULL;
}

static bool
is_format_supported(struct comp_compositor *c, VkFormat format)
{
	VkFormatProperties prop;
	c->vk.vkGetPhysicalDeviceFormatProperties(c->vk.physical_device, format,
	                                          &prop);

	// This is a fairly crude way of checking support,
	// but works well enough.
	return prop.optimalTilingFeatures != 0;
}

#define ADD_IF_SUPPORTED(format)                                               \
	do {                                                                   \
		if (is_format_supported(c, format)) {                          \
			info->formats[formats++] = format;                     \
		}                                                              \
	} while (false)

struct xrt_compositor_native *
xrt_gfx_provider_create_native(struct xrt_device *xdev)
{
	struct comp_compositor *c = U_TYPED_CALLOC(struct comp_compositor);

	c->base.base.create_swapchain = comp_swapchain_create;
	c->base.base.import_swapchain = comp_swapchain_import;
	c->base.base.prepare_session = compositor_prepare_session;
	c->base.base.begin_session = compositor_begin_session;
	c->base.base.end_session = compositor_end_session;
	c->base.base.wait_frame = compositor_wait_frame;
	c->base.base.begin_frame = compositor_begin_frame;
	c->base.base.discard_frame = compositor_discard_frame;
	c->base.base.layer_begin = compositor_layer_begin;
	c->base.base.layer_stereo_projection =
	    compositor_layer_stereo_projection;
	c->base.base.layer_stereo_projection_depth =
	    compositor_layer_stereo_projection_depth;
	c->base.base.layer_quad = compositor_layer_quad;
	c->base.base.layer_cube = compositor_layer_cube;
	c->base.base.layer_cylinder = compositor_layer_cylinder;
	c->base.base.layer_equirect = compositor_layer_equirect;
	c->base.base.layer_commit = compositor_layer_commit;
	c->base.base.poll_events = compositor_poll_events;
	c->base.base.destroy = compositor_destroy;
	c->xdev = xdev;

	u_threading_stack_init(&c->threading.destroy_swapchains);

	COMP_DEBUG(c, "Doing init %p", (void *)c);

	// Init the settings to default.
	comp_settings_init(&c->settings, xdev);

	c->last_frame_time_ns = os_monotonic_get_ns();
	c->frame_overhead_ns = 2000000;
	//! @todo set this to an estimate that's better than 6ms
	c->expected_app_duration_ns = 6000000;


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
		COMP_DEBUG(c, "Failed to init compositor %p", (void *)c);
		c->base.base.destroy(&c->base.base);
		return NULL;
	}
	// clang-format on

	c->window->update_window_title(c->window, WINDOW_TITLE);

	COMP_DEBUG(c, "Done %p", (void *)c);

	struct xrt_compositor_info *info = &c->base.base.info;

	// Required by OpenXR spec.
	info->max_layers = 16;

	memcpy(info->compositor_vk_deviceUUID,
	       c->settings.selected_gpu_deviceUUID, XRT_GPU_UUID_SIZE);

	memcpy(info->client_vk_deviceUUID, c->settings.client_gpu_deviceUUID,
	       XRT_GPU_UUID_SIZE);

	/*!
	 * @todo Support more like, depth/float formats etc,
	 * remember to update the GL client as well.
	 */
	/*
	 * These are the available formats we will expose to our clients.
	 *
	 * In order of what we prefer. Start with a SRGB format that works on
	 * both OpenGL and Vulkan. The two linear formats that works on both
	 * OpenGL and Vulkan. A SRGB format that only works on Vulkan. The last
	 * two formats should not be used as they are linear but doesn't have
	 * enough bits to express it without resulting in banding.
	 */
	uint32_t formats = 0;

	// color formats
	ADD_IF_SUPPORTED(VK_FORMAT_R8G8B8A8_SRGB);            // OGL VK
	ADD_IF_SUPPORTED(VK_FORMAT_A2B10G10R10_UNORM_PACK32); // OGL VK
	ADD_IF_SUPPORTED(VK_FORMAT_R16G16B16A16_SFLOAT);      // OGL VK
	ADD_IF_SUPPORTED(VK_FORMAT_B8G8R8A8_SRGB);            // VK
	ADD_IF_SUPPORTED(VK_FORMAT_R8G8B8A8_UNORM);           // OGL VK
	ADD_IF_SUPPORTED(VK_FORMAT_B8G8R8A8_UNORM);           // VK

	// depth formats
	ADD_IF_SUPPORTED(VK_FORMAT_D16_UNORM);  // OGL VK
	ADD_IF_SUPPORTED(VK_FORMAT_D32_SFLOAT); // OGL VK

	// depth stencil formats
	ADD_IF_SUPPORTED(VK_FORMAT_D24_UNORM_S8_UINT);  // OGL VK
	ADD_IF_SUPPORTED(VK_FORMAT_D32_SFLOAT_S8_UINT); // OGL VK

	assert(formats <= XRT_MAX_SWAPCHAIN_FORMATS);
	info->num_formats = formats;

	float scale = c->settings.viewport_scale;

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

	// clang-format off
	info->views[0].recommended.width_pixels  = w0;
	info->views[0].recommended.height_pixels = h0;
	info->views[0].recommended.sample_count  = 1;
	info->views[0].max.width_pixels          = w0_2;
	info->views[0].max.height_pixels         = h0_2;
	info->views[0].max.sample_count          = 1;

	info->views[1].recommended.width_pixels  = w1;
	info->views[1].recommended.height_pixels = h1;
	info->views[1].recommended.sample_count  = 1;
	info->views[1].max.width_pixels          = w1_2;
	info->views[1].max.height_pixels         = h1_2;
	info->views[1].max.sample_count          = 1;
	// clang-format on

	u_var_add_root(c, "Compositor", true);
	u_var_add_ro_f32(c, &c->compositor_frame_times.fps, "FPS (Compositor)");

	struct u_var_timing *ft = U_TYPED_CALLOC(struct u_var_timing);

	float target_frame_time_ms =
	    c->settings.nominal_frame_interval_ns * 1. / 1000. * 1. / 1000.;

	uint64_t now = os_monotonic_get_ns();
	for (int i = 0; i < NUM_FRAME_TIMES; i++) {
		c->compositor_frame_times.times_ns[i] = now + i;
	}
	ft->values.data = c->compositor_frame_times.timings_ms;
	ft->values.length = NUM_FRAME_TIMES;
	ft->values.index_ptr = &c->compositor_frame_times.index;

	ft->reference_timing = target_frame_time_ms;
	ft->range = 10.f;
	ft->unit = "ms";
	ft->dynamic_rescale = false;
	ft->center_reference_timing = true;

	u_var_add_f32_timing(c, ft, "Frame Times (Compositor)");

	c->compositor_frame_times.debug_var = ft;

	return &c->base;
}

void
comp_compositor_garbage_collect(struct comp_compositor *c)
{
	struct comp_swapchain *sc;

	while ((sc = u_threading_stack_pop(&c->threading.destroy_swapchains))) {
		comp_swapchain_really_destroy(sc);
	}
}
