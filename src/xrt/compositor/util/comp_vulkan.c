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

#define UUID_STR_SIZE (XRT_UUID_SIZE * 3 + 1)

static void
snprint_luid(char *str, size_t size, xrt_luid_t *luid)
{
	for (size_t i = 0, offset = 0; i < ARRAY_SIZE(luid->data) && offset < size; i++, offset += 3) {
		snprintf(str + offset, size - offset, "%02x ", luid->data[i]);
	}
}

static void
snprint_uuid(char *str, size_t size, xrt_uuid_t *uuid)
{
	for (size_t i = 0, offset = 0; i < ARRAY_SIZE(uuid->data) && offset < size; i++, offset += 3) {
		snprintf(str + offset, size - offset, "%02x ", uuid->data[i]);
	}
}

static bool
get_device_uuid(struct vk_bundle *vk, int gpu_index, xrt_uuid_t *uuid)
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
	memcpy(uuid->data, pdidp.deviceUUID, ARRAY_SIZE(uuid->data));

	return true;
}

static bool
get_device_luid(struct vk_bundle *vk, int gpu_index, xrt_luid_t *luid)
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
	if (pdidp.deviceLUIDValid != VK_TRUE) {
		return false;
	}
	memcpy(luid->data, pdidp.deviceLUID, ARRAY_SIZE(luid->data));

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
		if (get_device_uuid(vk, vk_res->selected_gpu_index, &vk_res->selected_gpu_deviceUUID)) {
			char uuid_str[UUID_STR_SIZE] = {0};
			snprint_uuid(uuid_str, ARRAY_SIZE(uuid_str), &vk_res->selected_gpu_deviceUUID);

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
		if (get_device_uuid(vk, vk_res->client_gpu_index, &vk_res->client_gpu_deviceUUID)) {
			char buffer[UUID_STR_SIZE] = {0};
			snprint_uuid(buffer, ARRAY_SIZE(buffer), &vk_res->client_gpu_deviceUUID);

			// Trailing space from snprint_uuid, means 'to' should be right next to '%s'.
			VK_DEBUG(vk, "Suggest %d with uuid: %sto clients", vk_res->client_gpu_index, buffer);

			if (get_device_luid(vk, vk_res->client_gpu_index, &vk_res->client_gpu_deviceLUID)) {
				vk_res->client_gpu_deviceLUID_valid = true;
				snprint_luid(buffer, ARRAY_SIZE(buffer), &vk_res->client_gpu_deviceLUID);
				VK_DEBUG(vk, "\tDevice LUID: %s", buffer);
			}
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

	assert(vk_args->required_instance_version != 0);

	struct u_string_list *instance_ext_list = vk_build_instance_extensions(
	    vk, vk_args->required_instance_extensions, vk_args->optional_instance_extensions);

	if (!instance_ext_list) {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}

	// Fill this out here.
	vk_fill_in_has_instance_extensions(vk, instance_ext_list);

	VkApplicationInfo app_info = {
	    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	    .pApplicationName = "Monado Compositor",
	    .pEngineName = "Monado",
	    .apiVersion = vk_args->required_instance_version,
	};

	VkInstanceCreateInfo instance_info = {
	    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	    .pApplicationInfo = &app_info,
	    .enabledExtensionCount = u_string_list_get_size(instance_ext_list),
	    .ppEnabledExtensionNames = u_string_list_get_data(instance_ext_list),
	};

	ret = vk->vkCreateInstance(&instance_info, NULL, &vk->instance);
	if (ret != VK_SUCCESS) {
		VK_ERROR_RET(vk, "vkCreateInstance", "Failed to create Vulkan instance", ret);
		return ret;
	}

	u_string_list_destroy(&instance_ext_list);

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
		ret = vk_create_device(                  //
		    vk,                                  //
		    vk_args->selected_gpu_index,         //
		    only_compute_queue,                  // compute_only
		    prios[i],                            // global_priority
		    vk_args->required_device_extensions, //
		    vk_args->optional_device_extensions, //
		    &device_features);                   // optional_device_features

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

	// Print device information.
	vk_print_opened_device_info(vk, U_LOGGING_INFO);

	// Print features enabled.
	vk_print_features_info(vk, U_LOGGING_INFO);

	// Now that we are done debug some used external handles.
	vk_print_external_handles_info(vk, U_LOGGING_INFO);

	return VK_SUCCESS;
}


/*
 *
 * Format checking function.
 *
 */

static bool
is_format_supported(struct vk_bundle *vk, VkFormat format, enum xrt_swapchain_usage_bits xbits)
{
	/*
	 * First check if the format is supported at all.
	 */

	VkFormatProperties prop;
	vk->vkGetPhysicalDeviceFormatProperties(vk->physical_device, format, &prop);
	const VkFormatFeatureFlagBits bits = prop.optimalTilingFeatures;

	if ((bits & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
		VK_DEBUG(vk, "Format '%s' can not be sampled from in optimal layout!", vk_format_string(format));
		return false;
	}

	if ((xbits & XRT_SWAPCHAIN_USAGE_COLOR) != 0 && (bits & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0) {
		VK_DEBUG(vk, "Color format '%s' can not be used as render target in optimal layout!",
		         vk_format_string(format));
		return false;
	}

	if ((xbits & XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL) != 0 &&
	    (bits & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
		VK_DEBUG(vk, "Depth/stencil format '%s' can not be used as render target in optimal layout!",
		         vk_format_string(format));
		return false;
	}


	/*
	 * Check exportability.
	 */

	VkExternalMemoryHandleTypeFlags handle_type = vk_csci_get_image_external_handle_type(vk);
	VkResult ret;

	VkImageUsageFlags usage = vk_csci_get_image_usage_flags(vk, format, xbits);

	// In->pNext
	VkPhysicalDeviceExternalImageFormatInfo external_image_format_info = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
	    .handleType = handle_type,
	};

	// In
	VkPhysicalDeviceImageFormatInfo2 format_info = {
	    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
	    .pNext = &external_image_format_info,
	    .format = format,
	    .type = VK_IMAGE_TYPE_2D,
	    .tiling = VK_IMAGE_TILING_OPTIMAL,
	    .usage = usage,
	};

	// Out->pNext
	VkExternalImageFormatProperties external_format_properties = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
	};

	// Out
	VkImageFormatProperties2 format_properties = {
	    .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
	    .pNext = &external_format_properties,
	};

	ret = vk->vkGetPhysicalDeviceImageFormatProperties2(vk->physical_device, &format_info, &format_properties);
	if (ret == VK_ERROR_FORMAT_NOT_SUPPORTED) {
		VK_DEBUG(vk, "Format '%s' as external image is not supported!", vk_format_string(format));
		return false;
	} else if (ret != VK_SUCCESS) {
		// This is not a expected path.
		VK_ERROR(vk, "vkGetPhysicalDeviceImageFormatProperties2: %s for format '%s'", vk_result_string(ret),
		         vk_format_string(format));
		return false;
	}

	VkExternalMemoryFeatureFlags features =
	    external_format_properties.externalMemoryProperties.externalMemoryFeatures;

	if ((features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) == 0) {
		VK_DEBUG(vk, "Format '%s' is not importable!", vk_format_string(format));
		return false;
	}

	if ((features & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) == 0) {
		VK_DEBUG(vk, "Format '%s' is not exportable!", vk_format_string(format));
		return false;
	}

	return true;
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

void
comp_vulkan_formats_check(struct vk_bundle *vk, struct comp_vulkan_formats *formats)
{
#define CHECK_COLOR(FORMAT)                                                                                            \
	formats->has_##FORMAT = is_format_supported(vk, VK_FORMAT_##FORMAT, XRT_SWAPCHAIN_USAGE_COLOR);
#define CHECK_DS(FORMAT)                                                                                               \
	formats->has_##FORMAT = is_format_supported(vk, VK_FORMAT_##FORMAT, XRT_SWAPCHAIN_USAGE_DEPTH_STENCIL);

	VK_CSCI_FORMATS(CHECK_COLOR, CHECK_DS, CHECK_DS, CHECK_DS)

#undef CHECK_COLOR
#undef CHECK_DS

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	/*
	 * Some Vulkan drivers will natively support importing and exporting
	 * SRGB formats (Qualcomm) even tho technically that's not intended
	 * by the AHardwareBuffer since they don't support sRGB formats.
	 * While others (Mail) does not support importing and exporting sRGB
	 * formats.
	 */
	if (!formats->has_R8G8B8A8_SRGB && formats->has_R8G8B8A8_UNORM) {
		formats->has_R8G8B8A8_SRGB = true;
		formats->emulated_R8G8B8A8_SRGB = true;
	}
#endif
}

void
comp_vulkan_formats_copy_to_info(const struct comp_vulkan_formats *formats, struct xrt_compositor_info *info)
{
	uint32_t format_count = 0;

#define ADD_IF_SUPPORTED(FORMAT)                                                                                       \
	if (formats->has_##FORMAT) {                                                                                   \
		info->formats[format_count++] = VK_FORMAT_##FORMAT;                                                    \
	}

	VK_CSCI_FORMATS(ADD_IF_SUPPORTED, ADD_IF_SUPPORTED, ADD_IF_SUPPORTED, ADD_IF_SUPPORTED)

#undef ADD_IF_SUPPORTED

	assert(format_count <= XRT_MAX_SWAPCHAIN_FORMATS);
	info->format_count = format_count;
}

void
comp_vulkan_formats_log(enum u_logging_level log_level, const struct comp_vulkan_formats *formats)
{
#define PRINT_NAME(FORMAT) "\n\tVK_FORMAT_" #FORMAT ": %s"
#define PRINT_BOOLEAN(FORMAT) , formats->has_##FORMAT ? "true" : "false"

	U_LOG_IFL_I(log_level, "Supported formats:"                                             //
	            VK_CSCI_FORMATS(PRINT_NAME, PRINT_NAME, PRINT_NAME, PRINT_NAME)             //
	            VK_CSCI_FORMATS(PRINT_BOOLEAN, PRINT_BOOLEAN, PRINT_BOOLEAN, PRINT_BOOLEAN) //
	);

#undef PRINT_NAME
#undef PRINT_BOOLEAN

#if defined(XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER)
	U_LOG_IFL_I(log_level,
	            "Emulated formats:"
	            "\n\tVK_FORMAT_R8G8B8A8_SRGB: %s",
	            formats->emulated_R8G8B8A8_SRGB ? "emulated" : "native");
#endif
}
