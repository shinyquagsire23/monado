// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Vulkan code for compositors.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup comp_util
 */

#include "os/os_time.h"

#include "util/u_handles.h"
#include "util/u_trace_marker.h"

#include "util/comp_vulkan.h"


/*
 *
 * Helper functions.
 *
 */

#define VK_ERROR_RET(VK, FUNC, MSG, RET) VK_ERROR(VK, FUNC ": %s\n\t" MSG, vk_result_string(RET))

static bool
get_device_uuid(struct vk_bundle *vk, int gpu_index, uint8_t *uuid)
{
	VkPhysicalDeviceIDProperties pdidp = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
	};

	VkPhysicalDeviceProperties2 pdp2 = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
	    .pNext = &pdidp,
	};

	VkPhysicalDevice phys[16];
	uint32_t gpu_count = ARRAY_SIZE(phys);
	VkResult ret;

	ret = vk->vkEnumeratePhysicalDevices(vk->instance, &gpu_count, phys);
	if (ret != VK_SUCCESS) {
		VK_ERROR_RET(vk, "vkEnumeratePhysicalDevices", "Failed to enumerate physical devices.", ret);
		return false;
	}

	vk->vkGetPhysicalDeviceProperties2(phys[gpu_index], &pdp2);
	memcpy(uuid, pdidp.deviceUUID, XRT_GPU_UUID_SIZE);

	return true;
}

VkResult
fill_in_results(struct vk_bundle *vk, const struct comp_vulkan_arguments *vk_args, struct comp_vulkan_results *vk_res)
{
	// Grab the device index from the vk_bundle
	vk_res->selected_gpu_index = vk->physical_device_index;

	// Grab the suggested device index for the client to use
	vk_res->client_gpu_index = vk_args->client_gpu_index;

	// Store physical device UUID for compositor in settings
	if (vk_res->selected_gpu_index >= 0) {
		if (get_device_uuid(vk, vk_res->selected_gpu_index, vk_res->selected_gpu_deviceUUID)) {
			char uuid_str[XRT_GPU_UUID_SIZE * 3 + 1] = {0};
			for (int i = 0; i < XRT_GPU_UUID_SIZE; i++) {
				sprintf(uuid_str + i * 3, "%02x ", vk_res->selected_gpu_deviceUUID[i]);
			}
			VK_DEBUG(vk, "Selected %d with uuid: %s", vk_res->selected_gpu_index, uuid_str);
		} else {
			VK_ERROR(vk, "Failed to get device %d uuid", vk_res->selected_gpu_index);
		}
	}

	// By default suggest GPU used by compositor to clients
	if (vk_res->client_gpu_index < 0) {
		vk_res->client_gpu_index = vk_res->selected_gpu_index;
	}

	// Store physical device UUID suggested to clients in settings
	if (vk_res->client_gpu_index >= 0) {
		if (get_device_uuid(vk, vk_res->client_gpu_index, vk_res->client_gpu_deviceUUID)) {
			char uuid_str[XRT_GPU_UUID_SIZE * 3 + 1] = {0};
			for (int i = 0; i < XRT_GPU_UUID_SIZE; i++) {
				sprintf(uuid_str + i * 3, "%02x ", vk_res->client_gpu_deviceUUID[i]);
			}
			// Trailing space above, means 'to' should be right next to '%s'.
			VK_DEBUG(vk, "Suggest %d with uuid: %sto clients", vk_res->client_gpu_index, uuid_str);
		} else {
			VK_ERROR(vk, "Failed to get device %d uuid", vk_res->client_gpu_index);
		}
	}

	return VK_SUCCESS;
}


/*
 *
 * Creation functions.
 *
 */

static VkResult
create_instance(struct vk_bundle *vk, const struct comp_vulkan_arguments *vk_args)
{
	VkResult ret;

	VkApplicationInfo app_info = {
	    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	    .pApplicationName = "Monado Compositor",
	    .pEngineName = "Monado",
	    .apiVersion = VK_MAKE_VERSION(1, 0, 2),
	};

	VkInstanceCreateInfo instance_info = {
	    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	    .pApplicationInfo = &app_info,
	    .enabledExtensionCount = vk_args->instance_extensions.num,
	    .ppEnabledExtensionNames = vk_args->instance_extensions.array,
	};

	ret = vk->vkCreateInstance(&instance_info, NULL, &vk->instance);
	if (ret != VK_SUCCESS) {
		VK_ERROR_RET(vk, "vkCreateInstance", "Failed to create Vulkan instance", ret);
		return ret;
	}

	ret = vk_get_instance_functions(vk);
	if (ret != VK_SUCCESS) {
		VK_ERROR_RET(vk, "vk_get_instance_functions", "Failed to get Vulkan instance functions.", ret);
		return ret;
	}

	return ret;
}

static VkResult
create_device(struct vk_bundle *vk, const struct comp_vulkan_arguments *vk_args)
{
	VkResult ret;

	const char *prio_strs[3] = {
	    "realtime",
	    "high",
	    "normal",
	};

	VkQueueGlobalPriorityEXT prios[3] = {
	    VK_QUEUE_GLOBAL_PRIORITY_REALTIME_EXT, // This is the one we really want.
	    VK_QUEUE_GLOBAL_PRIORITY_HIGH_EXT,     // Probably not as good but something.
	    VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_EXT,   // Default fallback.
	};

	const bool only_compute_queue = vk_args->only_compute_queue;

	struct vk_device_features device_features = {
	    .shader_storage_image_write_without_format = true,
	    .null_descriptor = only_compute_queue,
	    .timeline_semaphore = vk_args->timeline_semaphore,
	};

	// No other way then to try to see if realtime is available.
	for (size_t i = 0; i < ARRAY_SIZE(prios); i++) {
		ret = vk_create_device(                        //
		    vk,                                        //
		    vk_args->selected_gpu_index,               //
		    only_compute_queue,                        // compute_only
		    prios[i],                                  // global_priority
		    vk_args->required_device_extensions.array, //
		    vk_args->required_device_extensions.num,   //
		    vk_args->optional_device_extensions.array, //
		    vk_args->optional_device_extensions.num,   //
		    &device_features);                         // optional_device_features

		// All ok!
		if (ret == VK_SUCCESS) {
			VK_INFO(vk, "Created device and %s queue with %s priority.",
			        only_compute_queue ? "compute" : "graphics", prio_strs[i]);
			break;
		}

		// Try a lower priority.
		if (ret == VK_ERROR_NOT_PERMITTED_EXT) {
			continue;
		}

		// Some other error!
		VK_ERROR_RET(vk, "vk_create_device", "Failed to create Vulkan device.", ret);
		return ret;
	}

	ret = vk_init_mutex(vk);
	if (ret != VK_SUCCESS) {
		VK_ERROR_RET(vk, "vk_init_mutex", "Failed to init mutex.", ret);
		return ret;
	}

	ret = vk_init_cmd_pool(vk);
	if (ret != VK_SUCCESS) {
		VK_ERROR_RET(vk, "vk_init_cmd_pool", "Failed to init command pool.", ret);
		return ret;
	}

	return VK_SUCCESS;
}


/*
 *
 * 'Exported' function.
 *
 */

bool
comp_vulkan_init_bundle(struct vk_bundle *vk,
                        const struct comp_vulkan_arguments *vk_args,
                        struct comp_vulkan_results *vk_res)
{
	VkResult ret;

	vk->log_level = vk_args->log_level;

	ret = vk_get_loader_functions(vk, vk_args->get_instance_proc_address);
	if (ret != VK_SUCCESS) {
		VK_ERROR_RET(vk, "vk_get_loader_functions", "Failed to get VkInstance get process address.", ret);
		return false;
	}

	ret = create_instance(vk, vk_args);
	if (ret != VK_SUCCESS) {
		// Error already reported.
		return false;
	}

	ret = create_device(vk, vk_args);
	if (ret != VK_SUCCESS) {
		// Error already reported.
		return false;
	}

	ret = fill_in_results(vk, vk_args, vk_res);
	if (ret != VK_SUCCESS) {
		// Error already reported.
		return false;
	}

	return true;
}
