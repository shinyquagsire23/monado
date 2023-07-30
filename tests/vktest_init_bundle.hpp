// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Vulkan code for tests.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */
#pragma once

#include <xrt/xrt_config_have.h>
#include <xrt/xrt_deleters.hpp>

#ifdef XRT_HAVE_VULKAN
#include <xrt/xrt_config_vulkan.h>
#include <xrt/xrt_vulkan_includes.h>
#include <vk/vk_helpers.h>
#include <util/comp_vulkan.h>
#include <util/u_string_list.hpp>


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
#ifdef VK_KHR_timeline_semaphore
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
#endif
};
using unique_string_list =
    std::unique_ptr<u_string_list, xrt::deleters::ptr_ptr_deleter<u_string_list, &u_string_list_destroy>>;

struct VkBundleDestroyer
{
	void
	operator()(struct vk_bundle *vk) const
	{
		if (!vk) {
			return;
		}
		if (vk->device != VK_NULL_HANDLE) {
			vk->vkDestroyDevice(vk->device, NULL);
			vk->device = VK_NULL_HANDLE;
		}
		if (vk->instance != VK_NULL_HANDLE) {
			vk->vkDestroyInstance(vk->instance, NULL);
			vk->instance = VK_NULL_HANDLE;
		}
		delete vk;
	}
};

static void
vk_bundle_destroy(struct vk_bundle *vk)
{
	if (!vk) {
		return;
	}
	if (vk->device != VK_NULL_HANDLE) {
		vk->vkDestroyDevice(vk->device, NULL);
		vk->device = VK_NULL_HANDLE;
	}
	if (vk->instance != VK_NULL_HANDLE) {
		vk->vkDestroyInstance(vk->instance, NULL);
		vk->instance = VK_NULL_HANDLE;
	}
	vk_deinit_mutex(vk);
	delete vk;
}
// using unique_vk_bundle = std::unique_ptr<struct vk_bundle, VkBundleDestroyer>;
using unique_vk_bundle =
    std::unique_ptr<struct vk_bundle, xrt::deleters::ptr_deleter<struct vk_bundle, &vk_bundle_destroy>>;

static inline bool
vktest_init_bundle(struct vk_bundle *vk)
{
	// every backend needs at least the common extensions
	unique_string_list required_instance_ext_list{
	    u_string_list_create_from_array(instance_extensions_common, ARRAY_SIZE(instance_extensions_common))};

	unique_string_list optional_instance_ext_list{u_string_list_create()};

	unique_string_list required_device_extension_list{
	    u_string_list_create_from_array(required_device_extensions, ARRAY_SIZE(required_device_extensions))};

	unique_string_list optional_device_extension_list{
	    u_string_list_create_from_array(optional_device_extensions, ARRAY_SIZE(optional_device_extensions))};

	U_ZERO(vk);
	comp_vulkan_arguments args{VK_MAKE_VERSION(1, 0, 0),
	                           vkGetInstanceProcAddr,
	                           required_instance_ext_list.get(),
	                           optional_instance_ext_list.get(),
	                           required_device_extension_list.get(),
	                           optional_device_extension_list.get(),
	                           U_LOGGING_TRACE,
	                           false /* only_compute_queue */,
	                           true /*timeline_semaphore*/,
	                           -1,
	                           -1};
	comp_vulkan_results results{};
	bool success = comp_vulkan_init_bundle(vk, &args, &results);

	// success = success && VK_SUCCESS == vk_init_mutex(vk);
	return success;
}

#else

struct vk_bundle
{
};

using unique_vk_bundle = std::unique_ptr<struct vk_bundle>;

#endif

static unique_vk_bundle
makeVkBundle()
{
	unique_vk_bundle ret{new vk_bundle};
	return ret;
}
