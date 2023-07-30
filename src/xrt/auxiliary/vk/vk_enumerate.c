// Copyright 2019-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Vulkan enumeration helpers code.
 *
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_vk
 */

#include "xrt/xrt_handles.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "vk/vk_helpers.h"


/*
 *
 * Helpers.
 *
 */

#define CHECK_FIRST_CALL(FUNC, RET, COUNT)                                                                             \
	do {                                                                                                           \
		if (vk_has_error(RET, FUNC, __FILE__, __LINE__)) {                                                     \
			return RET;                                                                                    \
		}                                                                                                      \
		if (COUNT == 0) {                                                                                      \
			goto out;                                                                                      \
		}                                                                                                      \
	} while (false)

#define CHECK_SECOND_CALL(FUNC, RET, TO_FREE)                                                                          \
	do {                                                                                                           \
		if (vk_has_error(RET, FUNC, __FILE__, __LINE__)) {                                                     \
			free(TO_FREE);                                                                                 \
			return RET;                                                                                    \
		}                                                                                                      \
	} while (false)


/*
 *
 * 'Exported' functions.
 *
 */

VkResult
vk_enumerate_instance_extensions_properties(struct vk_bundle *vk,
                                            const char *layer_name,
                                            uint32_t *out_prop_count,
                                            VkExtensionProperties **out_props)
{
	VkExtensionProperties *props = NULL;
	uint32_t prop_count = 0;
	VkResult ret;

	ret = vk->vkEnumerateInstanceExtensionProperties(layer_name, &prop_count, NULL);
	CHECK_FIRST_CALL("vkEnumerateInstanceExtensionProperties", ret, prop_count);

	props = U_TYPED_ARRAY_CALLOC(VkExtensionProperties, prop_count);
	ret = vk->vkEnumerateInstanceExtensionProperties(layer_name, &prop_count, props);
	CHECK_SECOND_CALL("vkEnumerateInstanceExtensionProperties", ret, props);

out:
	*out_prop_count = prop_count;
	*out_props = props;

	return VK_SUCCESS;
}

VkResult
vk_enumerate_physical_devices(struct vk_bundle *vk,
                              uint32_t *out_physical_device_count,
                              VkPhysicalDevice **out_physical_devices)
{
	VkPhysicalDevice *physical_devices = NULL;
	uint32_t physical_device_count = 0;
	VkResult ret;

	ret = vk->vkEnumeratePhysicalDevices(vk->instance, &physical_device_count, NULL);
	CHECK_FIRST_CALL("vkEnumeratePhysicalDevices", ret, physical_device_count);

	physical_devices = U_TYPED_ARRAY_CALLOC(VkPhysicalDevice, physical_device_count);
	ret = vk->vkEnumeratePhysicalDevices(vk->instance, &physical_device_count, physical_devices);
	CHECK_SECOND_CALL("vkEnumeratePhysicalDevices", ret, physical_devices);

out:
	*out_physical_device_count = physical_device_count;
	*out_physical_devices = physical_devices;

	return VK_SUCCESS;
}

VkResult
vk_enumerate_physical_device_extension_properties(struct vk_bundle *vk,
                                                  VkPhysicalDevice physical_device,
                                                  const char *layer_name,
                                                  uint32_t *out_prop_count,
                                                  VkExtensionProperties **out_props)
{
	VkExtensionProperties *props = NULL;
	uint32_t prop_count = 0;
	VkResult ret;

	ret = vk->vkEnumerateDeviceExtensionProperties(physical_device, layer_name, &prop_count, NULL);
	CHECK_FIRST_CALL("vkEnumerateDeviceExtensionProperties", ret, prop_count);

	props = U_TYPED_ARRAY_CALLOC(VkExtensionProperties, prop_count);
	ret = vk->vkEnumerateDeviceExtensionProperties(physical_device, layer_name, &prop_count, props);
	CHECK_SECOND_CALL("vkEnumerateDeviceExtensionProperties", ret, props);

out:
	*out_prop_count = prop_count;
	*out_props = props;

	return VK_SUCCESS;
}

#ifdef VK_USE_PLATFORM_DISPLAY_KHR
VkResult
vk_enumerate_physical_device_display_properties(struct vk_bundle *vk,
                                                VkPhysicalDevice physical_device,
                                                uint32_t *out_prop_count,
                                                VkDisplayPropertiesKHR **out_props)
{
	VkDisplayPropertiesKHR *props = NULL;
	uint32_t prop_count = 0;
	VkResult ret;

	ret = vk->vkGetPhysicalDeviceDisplayPropertiesKHR(physical_device, &prop_count, NULL);
	CHECK_FIRST_CALL("vkGetPhysicalDeviceDisplayPropertiesKHR", ret, prop_count);

	props = U_TYPED_ARRAY_CALLOC(VkDisplayPropertiesKHR, prop_count);
	ret = vk->vkGetPhysicalDeviceDisplayPropertiesKHR(physical_device, &prop_count, props);
	CHECK_SECOND_CALL("vkGetPhysicalDeviceDisplayPropertiesKHR", ret, props);

out:
	*out_props = props;
	*out_prop_count = prop_count;

	return VK_SUCCESS;
}
#endif
