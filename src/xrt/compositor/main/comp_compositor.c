// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main compositor written using Vulkan implementation.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup comp
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "util/u_debug.h"
#include "util/u_misc.h"
#include "util/u_time.h"

#include "main/comp_compositor.h"
#include "main/comp_client_interface.h"

#include <unistd.h>

static void
compositor_destroy(struct xrt_compositor *xc)
{
	struct comp_compositor *c = comp_compositor(xc);
	struct vk_bundle *vk = &c->vk;

	COMP_DEBUG(c, "DESTROY");

	if (c->r) {
		comp_renderer_destroy(c->r);
		c->r = NULL;
	}

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

	vk_destroy_validation_callback(vk);

	if (vk->instance != VK_NULL_HANDLE) {
		vk->vkDestroyInstance(vk->instance, NULL);
		vk->instance = VK_NULL_HANDLE;
	}

	free(c);
}

static void
compositor_begin_session(struct xrt_compositor *xc, enum xrt_view_type type)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_DEBUG(c, "BEGIN_SESSION");
}

static void
compositor_end_session(struct xrt_compositor *xc)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_DEBUG(c, "END_SESSION");
}

static void
compositor_wait_frame(struct xrt_compositor *xc,
                      int64_t *predicted_display_time,
                      int64_t *predicted_display_period)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_SPEW(c, "WAIT_FRAME");

	// A little bit easier to read.
	int64_t interval_ns = (int64_t)c->settings.nominal_frame_interval_ns;

	// HACK: Wait until the frame is predicted to be displayed.
	// This needs improvement, but blocks for plausible timings.
	int64_t now_ns = time_state_get_now(c->timekeeping);
	int64_t already_used_ns = now_ns - c->last_frame_time_ns;
	int64_t remaining_usec = (interval_ns - already_used_ns) / 1000;

	// leave 1.25 ms for overhead
	if (remaining_usec > 1250) {
		usleep(remaining_usec - 1250);
	}

	*predicted_display_period = interval_ns;
	*predicted_display_time = c->last_frame_time_ns + interval_ns;

	// if frame intervals were missed, keep adding intervals until we
	// predict the next one in the future. Also make sure we leave the
	// application at least half a frame interval for rendering.

	now_ns = time_state_get_now(c->timekeeping);
	while (*predicted_display_time < now_ns - interval_ns / 2) {
		*predicted_display_time += interval_ns;
	}
}

static void
compositor_begin_frame(struct xrt_compositor *xc)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_SPEW(c, "BEGIN_FRAME");
}

static void
compositor_discard_frame(struct xrt_compositor *xc)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_SPEW(c, "DISCARD_FRAME");
}

static void
compositor_end_frame(struct xrt_compositor *xc,
                     enum xrt_blend_mode blend_mode,
                     struct xrt_swapchain **xscs,
                     const uint32_t *image_index,
                     uint32_t *layers,
                     uint32_t num_swapchains)
{
	struct comp_compositor *c = comp_compositor(xc);
	COMP_SPEW(c, "END_FRAME");

	struct comp_swapchain_image *right;
	struct comp_swapchain_image *left;

	// Stereo!
	if (num_swapchains == 2) {
		left = &comp_swapchain(xscs[0])->images[image_index[0]];
		right = &comp_swapchain(xscs[1])->images[image_index[1]];
		comp_renderer_frame(c->r, left, layers[0], right, layers[1]);
	} else {
		COMP_ERROR(c, "non-stereo rendering not supported");
	}

	// Record the time of this frame.
	c->last_frame_time_ns = time_state_get_now(c->timekeeping);
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

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName);

static VkResult
find_get_instance_proc_addr(struct comp_compositor *c)
{
	//! @todo Do any library loading here.
	return vk_get_loader_functions(&c->vk, vkGetInstanceProcAddr);
}

#ifdef XRT_ENABLE_VK_VALIDATION
#define COMPOSITOR_DEBUG_VULKAN_EXTENSIONS VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#else
#define COMPOSITOR_DEBUG_VULKAN_EXTENSIONS
#endif

#define COMPOSITOR_COMMON_VULKAN_EXTENSIONS                                    \
	COMPOSITOR_DEBUG_VULKAN_EXTENSIONS                                     \
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
#ifdef VK_USE_PLATFORM_XCB_KHR
	case WINDOW_XCB:
		*out_exts = instance_extensions_xcb;
		*out_num = ARRAY_SIZE(instance_extensions_xcb);
		break;
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	case WINDOW_WAYLAND:
		*out_exts = instance_extensions_wayland;
		*out_num = ARRAY_SIZE(instance_extensions_wayland);
		break;
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
	case WINDOW_DIRECT_RANDR:
	case WINDOW_DIRECT_NVIDIA:
		*out_exts = instance_extensions_direct_mode;
		*out_num = ARRAY_SIZE(instance_extensions_direct_mode);
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
	    .pNext = NULL,
	    .pApplicationName = "Collabora Compositor",
	    .applicationVersion = 0,
	    .pEngineName = "Monado",
	    .engineVersion = 0,
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
	    .pNext = NULL,
	    .flags = 0,
	    .pApplicationInfo = &app_info,
	    .enabledLayerCount = 0,
	    .ppEnabledLayerNames = NULL,
	    .enabledExtensionCount = num_extensions,
	    .ppEnabledExtensionNames = instance_extensions,
	};

#ifdef XRT_ENABLE_VK_VALIDATION
	const char *instance_layers[] = {
	    "VK_LAYER_LUNARG_standard_validation",
	};

	if (c->settings.validate_vulkan) {
		instance_info.enabledLayerCount = ARRAY_SIZE(instance_layers);
		instance_info.ppEnabledLayerNames = instance_layers;
	}
#endif

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

#ifdef XRT_ENABLE_VK_VALIDATION
	if (c->settings.validate_vulkan)
		vk_init_validation_callback(&c->vk);
#endif

	return ret;
}

static bool
compositor_init_vulkan(struct comp_compositor *c)
{

	VkResult ret;

	ret = find_get_instance_proc_addr(c);
	if (ret != VK_SUCCESS) {
		return false;
	}

	ret = create_instance(c);
	if (ret != VK_SUCCESS) {
		return false;
	}

	ret = vk_create_device(&c->vk);
	if (ret != VK_SUCCESS) {
		return false;
	}

	ret = vk_init_cmd_pool(&c->vk);
	return ret == VK_SUCCESS;
}

/*
 *
 * Other functions.
 *
 */

void
comp_compositor_print(struct comp_compositor *c,
                      const char *func,
                      const char *fmt,
                      ...)
{
	fprintf(stderr, "%s - ", func);

	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
}

static bool
compositor_check_vulkan_caps(struct comp_compositor *c)
{
	VkResult ret;

	// this is duplicative, but seems to be the easiest way to 'pre-check'
	// capabilities when window creation precedes vulkan instance creation.
	// we also need to load the VK_KHR_DISPLAY extension.

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
	    VK_KHR_DISPLAY_EXTENSION_NAME,
	};

	VkInstanceCreateInfo instance_create_info = {
	    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	    .pNext = NULL,
	    .flags = 0,
	    .pApplicationInfo = NULL,
	    .enabledLayerCount = 0,
	    .ppEnabledLayerNames = NULL,
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
	ret = vk_create_device(&temp_vk);
	if (ret != VK_SUCCESS) {
		COMP_ERROR(c, "Failed to create VkDevice: %s",
		           vk_result_string(ret));
		return false;
	}

	bool nvidia_tests_passed = false;

	VkPhysicalDeviceProperties physical_device_properties;
	temp_vk.vkGetPhysicalDeviceProperties(temp_vk.physical_device,
	                                      &physical_device_properties);

	if (physical_device_properties.vendorID == 0x10DE) {
		// our physical device is an nvidia card, we can potentially
		// select nvidia-specific direct mode.

		// we need to also check if we are confident that we can create
		// a direct mode display, if not we need to  abandon the attempt
		// here, and allow desktop-window fallback to occur.

		// get a list of attached displays
		uint32_t display_count;

		if (temp_vk.vkGetPhysicalDeviceDisplayPropertiesKHR(
		        temp_vk.physical_device, &display_count, NULL) !=
		    VK_SUCCESS) {
			COMP_ERROR(c, "Failed to get vulkan display count");
			nvidia_tests_passed = false;
		}

		VkDisplayPropertiesKHR *display_props =
		    U_TYPED_ARRAY_CALLOC(VkDisplayPropertiesKHR, display_count);

		if (display_props &&
		    temp_vk.vkGetPhysicalDeviceDisplayPropertiesKHR(
		        temp_vk.physical_device, &display_count,
		        display_props) != VK_SUCCESS) {
			COMP_ERROR(c, "Failed to get display properties");
			nvidia_tests_passed = false;
		}

		for (uint32_t i = 0; i < display_count; i++) {
			VkDisplayPropertiesKHR disp = *(display_props + i);
			// check this display against our whitelist
			uint32_t wl_elements = sizeof(NV_DIRECT_WHITELIST) /
			                       sizeof(NV_DIRECT_WHITELIST[0]);
			for (uint32_t j = 0; j < wl_elements; j++) {
				unsigned long wl_entry_length =
				    strlen(NV_DIRECT_WHITELIST[j]);
				unsigned long disp_entry_length =
				    strlen(disp.displayName);
				if (disp_entry_length >= wl_entry_length) {
					if (strncmp(NV_DIRECT_WHITELIST[j],
					            disp.displayName,
					            wl_entry_length) == 0) {
						// we have a match with this
						// whitelist entry.
						nvidia_tests_passed = true;
					}
				}
			}
		}

		free(display_props);
	}

	if (nvidia_tests_passed) {
		c->settings.window_type = WINDOW_DIRECT_NVIDIA;
		COMP_DEBUG(c, "Selecting direct NVIDIA window type!");
	} else {
		COMP_DEBUG(c, "Keeping auto window type!");
	}

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
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
		if (compositor_try_window(c, comp_window_direct_create(c))) {
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
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
		if (compositor_try_window(c, comp_window_wayland_create(c))) {
			c->settings.window_type = WINDOW_WAYLAND;
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
		compositor_try_window(c, comp_window_direct_create(c));
#else
		COMP_ERROR(c, "Direct mode support not compiled in!");
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

	return compositor_try_window(c, comp_window_direct_create(c));
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
compositor_init_renderer(struct comp_compositor *c)
{
	c->r = comp_renderer_create(c);
	return c->r != NULL;
}


struct xrt_compositor_fd *
comp_compositor_create(struct xrt_device *xdev,
                       struct time_state *timekeeping,
                       bool flip_y)
{
	struct comp_compositor *c = U_TYPED_CALLOC(struct comp_compositor);

	c->base.base.create_swapchain = comp_swapchain_create;
	c->base.base.begin_session = compositor_begin_session;
	c->base.base.end_session = compositor_end_session;
	c->base.base.wait_frame = compositor_wait_frame;
	c->base.base.begin_frame = compositor_begin_frame;
	c->base.base.discard_frame = compositor_discard_frame;
	c->base.base.end_frame = compositor_end_frame;
	c->base.base.destroy = compositor_destroy;
	c->xdev = xdev;
	c->timekeeping = timekeeping;

	COMP_DEBUG(c, "Doing init %p", (void *)c);

	// Init the settings to default.
	comp_settings_init(&c->settings, xdev);

	c->settings.flip_y = flip_y;
	c->last_frame_time_ns = time_state_get_now(c->timekeeping);


	// Need to select window backend before creating Vulkan, then
	// swapchain will initialize the window fully and the swapchain,
	// and finally the renderer is created which renderers to
	// window/swapchain.

	// clang-format off
	if (!compositor_check_vulkan_caps(c) ||
	    !compositor_init_window_pre_vulkan(c) ||
	    !compositor_init_vulkan(c) ||
	    !compositor_init_window_post_vulkan(c) ||
	    !compositor_init_swapchain(c) ||
	    !compositor_init_renderer(c)) {
		COMP_DEBUG(c, "Failed to init compositor %p", (void *)c);
		c->base.base.destroy(&c->base.base);
		return NULL;
	}
	// clang-format on

	COMP_DEBUG(c, "Done %p", (void *)c);

	return &c->base;
}
