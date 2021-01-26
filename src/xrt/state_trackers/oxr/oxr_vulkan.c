// Copyright 2018-2020, Collabora, Ltd.
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
#include "util/u_debug.h"

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

	OXR_TWO_CALL_HELPER(log, namesCapacityInput, namesCountOutput, namesString, length,
	                    xrt_gfx_vk_instance_extensions, XR_SUCCESS);
}

XrResult
oxr_vk_get_device_exts(struct oxr_logger *log,
                       struct oxr_system *sys,
                       uint32_t namesCapacityInput,
                       uint32_t *namesCountOutput,
                       char *namesString)
{
	size_t length = strlen(xrt_gfx_vk_device_extensions) + 1;

	OXR_TWO_CALL_HELPER(log, namesCapacityInput, namesCountOutput, namesString, length,
	                    xrt_gfx_vk_device_extensions, XR_SUCCESS);
}

XrResult
oxr_vk_get_requirements(struct oxr_logger *log,
                        struct oxr_system *sys,
                        XrGraphicsRequirementsVulkanKHR *graphicsRequirements)
{
	struct xrt_api_requirements ver;

	xrt_gfx_vk_get_versions(&ver);
	graphicsRequirements->minApiVersionSupported = XR_MAKE_VERSION(ver.min_major, ver.min_minor, ver.min_patch);
	graphicsRequirements->maxApiVersionSupported = XR_MAKE_VERSION(ver.max_major, ver.max_minor, ver.max_patch);

	sys->gotten_requirements = true;

	return XR_SUCCESS;
}

DEBUG_GET_ONCE_LOG_OPTION(compositor_log, "XRT_COMPOSITOR_LOG", U_LOGGING_WARN)

//! @todo extension lists are duplicated as long strings in comp_vk_glue.c
static const char *required_vk_instance_extensions[] = {
    VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
};

// The device extensions do vary by platform, but in a very regular way.
// This should match the list in comp_compositor, except it shouldn't include
// VK_KHR_SWAPCHAIN_EXTENSION_NAME
static const char *required_vk_device_extensions[] = {
    VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,      VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,           VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,

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
#if defined(XRT_GRAPHICS_SYNC_HANDLE_IS_FD)
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,     VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,

#elif defined(XRT_GRAPHICS_SYNC_HANDLE_IS_WIN32_HANDLE)
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
    VK_KHR_EXTERNAL_FENCE_WIN32_EXTENSION_NAME,

#else
#error "Need port!"
#endif
};

static void
find_to_add(char const *const *enabled,
            uint32_t num_enabled,
            const char **required,
            uint32_t num_required,
            const char **to_add,
            uint32_t *num_to_add)
{
	for (uint32_t i = 0; i < num_required; i++) {
		bool already_in_list = false;
		for (uint32_t j = 0; j < num_enabled; j++) {
			if (strcmp(enabled[j], required[i]) == 0) {
				already_in_list = true;
				break;
			}
		}

		if (!already_in_list) {
			to_add[(*num_to_add)++] = required[i];
		}
	}
}

static bool
extend_instance_extensions(struct oxr_logger *log, VkInstanceCreateInfo *create_info)
{
	uint32_t num_required = ARRAY_SIZE(required_vk_instance_extensions);

	uint32_t num_to_add = 0;
	const char *to_add[ARRAY_SIZE(required_vk_instance_extensions)];

	uint32_t num_enabled = create_info->enabledExtensionCount;
	char const *const *enabled = create_info->ppEnabledExtensionNames;

	find_to_add(enabled, num_enabled, required_vk_instance_extensions, num_required, to_add, &num_to_add);

	enum u_logging_level ll = debug_get_log_option_compositor_log();

	if (num_to_add == 0) {
		if (ll <= U_LOGGING_DEBUG) {
			oxr_log(log, "App enabled all required instance exts");
		}
		return false;
	}

	uint32_t total = num_enabled + num_to_add;
	char const **new_enabled = malloc(sizeof(char *) * total);

	for (uint32_t i = 0; i < num_enabled; i++) {
		new_enabled[i] = enabled[i];
		if (ll <= U_LOGGING_DEBUG) {
			oxr_log(log, "Instance ext (app): %s", enabled[i]);
		}
	}

	for (uint32_t i = 0; i < num_to_add; i++) {
		new_enabled[num_enabled + i] = to_add[i];
		if (ll <= U_LOGGING_DEBUG) {
			oxr_log(log, "Instance ext (rt): %s", to_add[i]);
		}
	}

	create_info->ppEnabledExtensionNames = new_enabled;
	create_info->enabledExtensionCount = total;

	return true;
}

static bool
extend_device_extensions(struct oxr_logger *log, VkDeviceCreateInfo *create_info)
{
	uint32_t num_required = ARRAY_SIZE(required_vk_device_extensions);

	uint32_t num_to_add = 0;
	const char *to_add[ARRAY_SIZE(required_vk_device_extensions)];

	uint32_t num_enabled = create_info->enabledExtensionCount;
	char const *const *enabled = create_info->ppEnabledExtensionNames;

	find_to_add(enabled, num_enabled, required_vk_device_extensions, num_required, to_add, &num_to_add);

	enum u_logging_level ll = debug_get_log_option_compositor_log();

	if (num_to_add == 0) {
		if (ll <= U_LOGGING_DEBUG) {
			oxr_log(log, "App enabled all required device exts");
		}
		return false;
	}

	uint32_t total = num_enabled + num_to_add;
	char const **new_enabled = malloc(sizeof(char *) * total);

	for (uint32_t i = 0; i < num_enabled; i++) {
		new_enabled[i] = enabled[i];
		if (ll <= U_LOGGING_DEBUG) {
			oxr_log(log, "Device ext (app): %s", enabled[i]);
		}
	}

	for (uint32_t i = 0; i < num_to_add; i++) {
		new_enabled[num_enabled + i] = to_add[i];
		if (ll <= U_LOGGING_DEBUG) {
			oxr_log(log, "Device ext (rt): %s", to_add[i]);
		}
	}

	create_info->ppEnabledExtensionNames = new_enabled;
	create_info->enabledExtensionCount = total;

	return true;
}

XrResult
oxr_vk_create_vulkan_instance(struct oxr_logger *log,
                              struct oxr_system *sys,
                              const XrVulkanInstanceCreateInfoKHR *createInfo,
                              VkInstance *vulkanInstance,
                              VkResult *vulkanResult)
{

	PFN_vkGetInstanceProcAddr GetInstanceProcAddr = createInfo->pfnGetInstanceProcAddr;

	PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)GetInstanceProcAddr(NULL, "vkCreateInstance");
	if (!CreateInstance) {
		//! @todo: clarify in spec
		*vulkanResult = VK_ERROR_INITIALIZATION_FAILED;
		return XR_SUCCESS;
	}

	const VkAllocationCallbacks *vulkanAllocator = createInfo->vulkanAllocator;

	VkInstanceCreateInfo modified_info = *createInfo->vulkanCreateInfo;
	bool free_list = extend_instance_extensions(log, &modified_info);

	*vulkanResult = CreateInstance(&modified_info, vulkanAllocator, vulkanInstance);

	if (free_list) {
		free((void *)modified_info.ppEnabledExtensionNames);
	}

	return XR_SUCCESS;
}

XrResult
oxr_vk_create_vulkan_device(struct oxr_logger *log,
                            struct oxr_system *sys,
                            const XrVulkanDeviceCreateInfoKHR *createInfo,
                            VkDevice *vulkanDevice,
                            VkResult *vulkanResult)
{
	PFN_vkGetInstanceProcAddr GetInstanceProcAddr = createInfo->pfnGetInstanceProcAddr;

	PFN_vkCreateDevice CreateDevice =
	    (PFN_vkCreateDevice)GetInstanceProcAddr(sys->vulkan_enable2_instance, "vkCreateDevice");
	if (!CreateDevice) {
		//! @todo: clarify in spec
		*vulkanResult = VK_ERROR_INITIALIZATION_FAILED;
		return XR_SUCCESS;
	}

	const VkAllocationCallbacks *vulkanAllocator = createInfo->vulkanAllocator;
	VkPhysicalDevice physical_device = createInfo->vulkanPhysicalDevice;

	VkDeviceCreateInfo modified_info = *createInfo->vulkanCreateInfo;
	bool free_list = extend_device_extensions(log, &modified_info);

	*vulkanResult = CreateDevice(physical_device, &modified_info, vulkanAllocator, vulkanDevice);

	if (free_list) {
		free((void *)modified_info.ppEnabledExtensionNames);
	}

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
	GET_PROC(vkGetPhysicalDeviceProperties2);
	VkResult vk_ret;
	uint32_t count;

	vk_ret = vkEnumeratePhysicalDevices(vkInstance, &count, NULL);
	if (vk_ret != VK_SUCCESS) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Call to vkEnumeratePhysicalDevices returned %u",
		                 vk_ret);
	}
	if (count == 0) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Call to vkEnumeratePhysicalDevices returned zero "
		                 "VkPhysicalDevices");
	}

	VkPhysicalDevice *phys = U_TYPED_ARRAY_CALLOC(VkPhysicalDevice, count);
	vk_ret = vkEnumeratePhysicalDevices(vkInstance, &count, phys);
	if (vk_ret != VK_SUCCESS) {
		free(phys);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Call to vkEnumeratePhysicalDevices returned %u",
		                 vk_ret);
	}
	if (count == 0) {
		free(phys);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Call to vkEnumeratePhysicalDevices returned zero "
		                 "VkPhysicalDevices");
	}

	char suggested_uuid_str[XRT_GPU_UUID_SIZE * 3 + 1] = {0};
	for (int i = 0; i < XRT_GPU_UUID_SIZE; i++) {
		sprintf(suggested_uuid_str + i * 3, "%02x ", sys->xsysc->info.client_vk_deviceUUID[i]);
	}

	enum u_logging_level ll = debug_get_log_option_compositor_log();
	int gpu_index = -1;
	for (uint32_t i = 0; i < count; i++) {
		VkPhysicalDeviceIDProperties pdidp = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};

		VkPhysicalDeviceProperties2 pdp2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		                                    .pNext = &pdidp};

		vkGetPhysicalDeviceProperties2(phys[i], &pdp2);

		char uuid_str[XRT_GPU_UUID_SIZE * 3 + 1] = {0};
		if (ll <= U_LOGGING_DEBUG) {
			for (int i = 0; i < XRT_GPU_UUID_SIZE; i++) {
				sprintf(uuid_str + i * 3, "%02x ", pdidp.deviceUUID[i]);
			}
			oxr_log(log, "GPU %d: uuid %s", i, uuid_str);
		}

		if (memcmp(pdidp.deviceUUID, sys->xsysc->info.client_vk_deviceUUID, XRT_GPU_UUID_SIZE) == 0) {
			gpu_index = i;
			if (ll <= U_LOGGING_DEBUG) {
				oxr_log(log,
				        "Using GPU %d with uuid %s suggested "
				        "by runtime",
				        gpu_index, uuid_str);
			}
			break;
		}
	}

	if (gpu_index == -1) {
		oxr_warn(log, "Did not find runtime suggested GPU, fall back to GPU 0");
		gpu_index = 0;
	}

	*vkPhysicalDevice = phys[gpu_index];

	// vulkan_enable2 needs the physical device in xrCreateVulkanDeviceKHR
	if (inst->extensions.KHR_vulkan_enable2) {
		sys->vulkan_enable2_instance = vkInstance;
		sys->vulkan_enable2_physical_device = *vkPhysicalDevice;
	}

	free(phys);

	return XR_SUCCESS;
}
