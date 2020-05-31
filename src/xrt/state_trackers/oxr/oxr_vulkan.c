// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds Vulkan related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "util/u_misc.h"

#include "xrt/xrt_gfx_vk.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"


#define GET_PROC(name) PFN_##name name = (PFN_##name)getProc(vkInstance, #name)

XrResult
oxr_vk_get_instance_exts(struct oxr_logger *log,
                         struct oxr_system *sys,
                         uint32_t namesCapacityInput,
                         uint32_t *namesCountOutput,
                         char *namesString)
{
	size_t length = strlen(xrt_gfx_vk_instance_extensions) + 1;

	OXR_TWO_CALL_HELPER(log, namesCapacityInput, namesCountOutput,
	                    namesString, length, xrt_gfx_vk_instance_extensions,
	                    XR_SUCCESS);
}

XrResult
oxr_vk_get_device_exts(struct oxr_logger *log,
                       struct oxr_system *sys,
                       uint32_t namesCapacityInput,
                       uint32_t *namesCountOutput,
                       char *namesString)
{
	size_t length = strlen(xrt_gfx_vk_device_extensions) + 1;

	OXR_TWO_CALL_HELPER(log, namesCapacityInput, namesCountOutput,
	                    namesString, length, xrt_gfx_vk_device_extensions,
	                    XR_SUCCESS);
}

XrResult
oxr_vk_get_requirements(struct oxr_logger *log,
                        struct oxr_system *sys,
                        XrGraphicsRequirementsVulkanKHR *graphicsRequirements)
{
	struct xrt_api_requirements ver;

	xrt_gfx_vk_get_versions(&ver);
	graphicsRequirements->minApiVersionSupported =
	    XR_MAKE_VERSION(ver.min_major, ver.min_minor, ver.min_patch);
	graphicsRequirements->maxApiVersionSupported =
	    XR_MAKE_VERSION(ver.max_major, ver.max_minor, ver.max_patch);

	sys->gotten_requirements = true;

	return XR_SUCCESS;
}

XrResult
oxr_vk_get_physical_device(struct oxr_logger *log,
                           struct oxr_instance *inst,
                           struct oxr_system *sys,
                           VkInstance vkInstance,
                           PFN_vkGetInstanceProcAddr getProc,
                           VkPhysicalDevice *vkPhysicalDevice)
{
	GET_PROC(vkEnumeratePhysicalDevices);
	GET_PROC(vkGetPhysicalDeviceProperties);
	VkResult vk_ret;
	uint32_t count;

	vk_ret = vkEnumeratePhysicalDevices(vkInstance, &count, NULL);
	if (vk_ret != VK_SUCCESS) {
		return oxr_error(
		    log, XR_ERROR_RUNTIME_FAILURE,
		    "Call to vkEnumeratePhysicalDevices returned %u", vk_ret);
	}
	if (count == 0) {
		return oxr_error(
		    log, XR_ERROR_RUNTIME_FAILURE,
		    "Call to vkEnumeratePhysicalDevices returned zero "
		    "VkPhysicalDevices");
	}

	VkPhysicalDevice *phys = U_TYPED_ARRAY_CALLOC(VkPhysicalDevice, count);
	vk_ret = vkEnumeratePhysicalDevices(vkInstance, &count, phys);
	if (vk_ret != VK_SUCCESS) {
		free(phys);
		return oxr_error(
		    log, XR_ERROR_RUNTIME_FAILURE,
		    "Call to vkEnumeratePhysicalDevices returned %u", vk_ret);
	}
	if (count == 0) {
		free(phys);
		return oxr_error(
		    log, XR_ERROR_RUNTIME_FAILURE,
		    "Call to vkEnumeratePhysicalDevices returned zero "
		    "VkPhysicalDevices");
	}

	if (count > 1) {
		OXR_WARN_ONCE(log,
		              "super intelligent device selection algorithm "
		              "can't handle more then one VkPhysicalDevice, "
		              "picking the first discrete gpu in the list.");
	}

	// as a first-step to 'intelligent' selection, prefer a 'discrete' gpu
	// if it is present
	uint32_t gpu_index = 0;
	for (uint32_t i = 0; i < count; i++) {
		VkPhysicalDeviceProperties pdp;
		vkGetPhysicalDeviceProperties(phys[i], &pdp);
		if (pdp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			gpu_index = i;
		}
	}

	*vkPhysicalDevice = phys[gpu_index];

	free(phys);

	return XR_SUCCESS;
}
