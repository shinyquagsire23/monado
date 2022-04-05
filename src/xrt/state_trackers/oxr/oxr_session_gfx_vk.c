// Copyright 2018-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds Vulkan specific session functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 * @ingroup comp_client
 */

#include "xrt/xrt_instance.h"
#include "xrt/xrt_gfx_vk.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "vk/vk_helpers.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_handle.h"


DEBUG_GET_ONCE_BOOL_OPTION(force_timeline_semaphores, "OXR_DEBUG_FORCE_TIMELINE_SEMAPHORES", false)


static bool
check_for_layer_mnd_enable_timeline_semaphore(struct oxr_logger *log,
                                              VkInstance instance,
                                              VkPhysicalDevice physical_device)
{
	PFN_vkEnumerateDeviceLayerProperties func =
	    (PFN_vkEnumerateDeviceLayerProperties)vkGetInstanceProcAddr(instance, "vkEnumerateDeviceLayerProperties");
	uint32_t prop_count = 0;
	VkResult ret = VK_SUCCESS;

	ret = func(physical_device, &prop_count, NULL);
	if (ret != VK_SUCCESS) {
		oxr_log(log, "vkEnumerateDeviceLayerProperties: %s", vk_result_string(ret));
		return false;
	}

	if (prop_count == 0) {
		// No layers, nothing more to do.
		return false;
	}

	VkLayerProperties *props = NULL;
	props = U_TYPED_ARRAY_CALLOC(VkLayerProperties, prop_count);

	ret = func(physical_device, &prop_count, props);
	if (ret != VK_SUCCESS) {
		oxr_log(log, "vkEnumerateDeviceLayerProperties: %s", vk_result_string(ret));
		free(props);
		return false;
	}

	for (uint32_t i = 0; i < prop_count; i++) {
		if (strcmp(props[i].layerName, "VK_LAYER_MND_enable_timeline_semaphore") == 0) {
			free(props);
			return true;
		}
	}

	free(props);
	return false;
}

XrResult
oxr_session_populate_vk(struct oxr_logger *log,
                        struct oxr_system *sys,
                        XrGraphicsBindingVulkanKHR const *next,
                        struct oxr_session *sess)
{
	bool timeline_semaphore_enabled = sess->sys->vk.timeline_semaphore_enabled;

	if (!timeline_semaphore_enabled &&
	    check_for_layer_mnd_enable_timeline_semaphore(log, next->instance, next->physicalDevice)) {
		oxr_log(log, "Found VK_LAYER_MND_enable_timeline_semaphore and enabled them!");
		timeline_semaphore_enabled = true;
	}

	if (!timeline_semaphore_enabled && debug_get_bool_option_force_timeline_semaphores()) {
		oxr_log(log, "Forcing timeline semaphores on, your app better have enabled them!");
		timeline_semaphore_enabled = true;
	}

	struct xrt_compositor_native *xcn = sess->xcn;
	struct xrt_compositor_vk *xcvk = xrt_gfx_vk_provider_create( //
	    xcn,                                                     //
	    next->instance,                                          //
	    vkGetInstanceProcAddr,                                   //
	    next->physicalDevice,                                    //
	    next->device,                                            //
	    timeline_semaphore_enabled,                              //
	    next->queueFamilyIndex,                                  //
	    next->queueIndex);                                       //

	if (xcvk == NULL) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED, "Failed to create an vk client compositor");
	}

	sess->compositor = &xcvk->base;
	sess->create_swapchain = oxr_swapchain_vk_create;

	return XR_SUCCESS;
}
