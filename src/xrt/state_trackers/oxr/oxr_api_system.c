// Copyright 2018-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds system related entrypoints.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_api
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_gfx_gl.h"
#include "xrt/xrt_gfx_gles.h"
#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"

#include "oxr_api_funcs.h"
#include "oxr_api_verify.h"


/*!
 * A helper define that verifies the systemId.
 */
#define OXR_VERIFY_SYSTEM_AND_GET(log, inst, sysId, system)                                                            \
	struct oxr_system *system = NULL;                                                                              \
	do {                                                                                                           \
		XrResult ret = oxr_system_get_by_id(log, inst, sysId, &system);                                        \
		if (ret != XR_SUCCESS) {                                                                               \
			return ret;                                                                                    \
		}                                                                                                      \
		assert(system != NULL);                                                                                \
	} while (false)

XrResult
oxr_xrGetSystem(XrInstance instance, const XrSystemGetInfo *getInfo, XrSystemId *systemId)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetSystem");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, getInfo, XR_TYPE_SYSTEM_GET_INFO);
	OXR_VERIFY_ARG_NOT_NULL(&log, systemId);

	struct oxr_system *selected = NULL;
	struct oxr_system *systems[1] = {&inst->system};
	uint32_t num_systems = 1;

	XrResult ret = oxr_system_select(&log, systems, num_systems, getInfo->formFactor, &selected);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*systemId = selected->systemId;

	return XR_SUCCESS;
}

XrResult
oxr_xrGetSystemProperties(XrInstance instance, XrSystemId systemId, XrSystemProperties *properties)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetSystemProperties");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, properties, XR_TYPE_SYSTEM_PROPERTIES);
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);

	return oxr_system_get_properties(&log, sys, properties);
}

XrResult
oxr_xrEnumerateViewConfigurations(XrInstance instance,
                                  XrSystemId systemId,
                                  uint32_t viewConfigurationTypeCapacityInput,
                                  uint32_t *viewConfigurationTypeCountOutput,
                                  XrViewConfigurationType *viewConfigurationTypes)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrEnumerateViewConfigurations");
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);

	return oxr_system_enumerate_view_confs(&log, sys, viewConfigurationTypeCapacityInput,
	                                       viewConfigurationTypeCountOutput, viewConfigurationTypes);
}

XrResult
oxr_xrEnumerateEnvironmentBlendModes(XrInstance instance,
                                     XrSystemId systemId,
                                     XrViewConfigurationType viewConfigurationType,
                                     uint32_t environmentBlendModeCapacityInput,
                                     uint32_t *environmentBlendModeCountOutput,
                                     XrEnvironmentBlendMode *environmentBlendModes)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrEnumerateEnvironmentBlendModes");
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);
	OXR_VERIFY_VIEW_CONFIG_TYPE(&log, inst, viewConfigurationType);

	if (viewConfigurationType != sys->view_config_type) {
		return oxr_error(&log, XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED,
		                 "(viewConfigurationType == 0x%08x) "
		                 "unsupported view configuration type",
		                 viewConfigurationType);
	}

	return oxr_system_enumerate_blend_modes(&log, sys, viewConfigurationType, environmentBlendModeCapacityInput,
	                                        environmentBlendModeCountOutput, environmentBlendModes);
}

XrResult
oxr_xrGetViewConfigurationProperties(XrInstance instance,
                                     XrSystemId systemId,
                                     XrViewConfigurationType viewConfigurationType,
                                     XrViewConfigurationProperties *configurationProperties)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetViewConfigurationProperties");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, configurationProperties, XR_TYPE_VIEW_CONFIGURATION_PROPERTIES);
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);

	return oxr_system_get_view_conf_properties(&log, sys, viewConfigurationType, configurationProperties);
}

XrResult
oxr_xrEnumerateViewConfigurationViews(XrInstance instance,
                                      XrSystemId systemId,
                                      XrViewConfigurationType viewConfigurationType,
                                      uint32_t viewCapacityInput,
                                      uint32_t *viewCountOutput,
                                      XrViewConfigurationView *views)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrEnumerateViewConfigurationViews");
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);

	return oxr_system_enumerate_view_conf_views(&log, sys, viewConfigurationType, viewCapacityInput,
	                                            viewCountOutput, views);
}


/*
 *
 * OpenGL ES
 *
 */

#ifdef XR_USE_GRAPHICS_API_OPENGL_ES

XrResult
oxr_xrGetOpenGLESGraphicsRequirementsKHR(XrInstance instance,
                                         XrSystemId systemId,
                                         XrGraphicsRequirementsOpenGLESKHR *graphicsRequirements)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetOpenGLESGraphicsRequirementsKHR");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, graphicsRequirements, XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR);
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);

	struct xrt_api_requirements ver;

	xrt_gfx_gles_get_versions(&ver);

	graphicsRequirements->minApiVersionSupported = XR_MAKE_VERSION(ver.min_major, ver.min_minor, ver.min_patch);
	graphicsRequirements->maxApiVersionSupported = XR_MAKE_VERSION(ver.max_major, ver.max_minor, ver.max_patch);

	sys->gotten_requirements = true;

	return XR_SUCCESS;
}

#endif


/*
 *
 * OpenGL
 *
 */

#ifdef XR_USE_GRAPHICS_API_OPENGL

XrResult
oxr_xrGetOpenGLGraphicsRequirementsKHR(XrInstance instance,
                                       XrSystemId systemId,
                                       XrGraphicsRequirementsOpenGLKHR *graphicsRequirements)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetOpenGLGraphicsRequirementsKHR");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, graphicsRequirements, XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR);
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);

	struct xrt_api_requirements ver;

	xrt_gfx_gl_get_versions(&ver);

	graphicsRequirements->minApiVersionSupported = XR_MAKE_VERSION(ver.min_major, ver.min_minor, ver.min_patch);
	graphicsRequirements->maxApiVersionSupported = XR_MAKE_VERSION(ver.max_major, ver.max_minor, ver.max_patch);

	sys->gotten_requirements = true;

	return XR_SUCCESS;
}

#endif


/*
 *
 * Vulkan
 *
 */

#ifdef XR_USE_GRAPHICS_API_VULKAN

XrResult
oxr_xrGetVulkanInstanceExtensionsKHR(XrInstance instance,
                                     XrSystemId systemId,
                                     uint32_t namesCapacityInput,
                                     uint32_t *namesCountOutput,
                                     char *namesString)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetVulkanInstanceExtensionsKHR");
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);

	return oxr_vk_get_instance_exts(&log, sys, namesCapacityInput, namesCountOutput, namesString);
}

XrResult
oxr_xrGetVulkanDeviceExtensionsKHR(XrInstance instance,
                                   XrSystemId systemId,
                                   uint32_t namesCapacityInput,
                                   uint32_t *namesCountOutput,
                                   char *namesString)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetVulkanDeviceExtensionsKHR");
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);

	return oxr_vk_get_device_exts(&log, sys, namesCapacityInput, namesCountOutput, namesString);
}

// NOLINTNEXTLINE // don't remove the forward decl.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName);

XrResult
oxr_xrGetVulkanGraphicsDeviceKHR(XrInstance instance,
                                 XrSystemId systemId,
                                 VkInstance vkInstance,
                                 VkPhysicalDevice *vkPhysicalDevice)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetVulkanGraphicsDeviceKHR");
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);
	OXR_VERIFY_ARG_NOT_NULL(&log, vkPhysicalDevice);

	return oxr_vk_get_physical_device(&log, inst, sys, vkInstance, vkGetInstanceProcAddr, vkPhysicalDevice);
}

XrResult
oxr_xrGetVulkanGraphicsDevice2KHR(XrInstance instance,
                                  const XrVulkanGraphicsDeviceGetInfoKHR *getInfo,
                                  VkPhysicalDevice *vkPhysicalDevice)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetVulkanGraphicsDeviceKHR");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, getInfo, XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR);

	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, getInfo->systemId, sys);
	OXR_VERIFY_ARG_NOT_NULL(&log, vkPhysicalDevice);

	return oxr_vk_get_physical_device(&log, inst, sys, getInfo->vulkanInstance, vkGetInstanceProcAddr,
	                                  vkPhysicalDevice);
}

XrResult
oxr_xrGetVulkanGraphicsRequirementsKHR(XrInstance instance,
                                       XrSystemId systemId,
                                       XrGraphicsRequirementsVulkanKHR *graphicsRequirements)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetVulkanGraphicsRequirementsKHR");
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, graphicsRequirements, XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR);

	return oxr_vk_get_requirements(&log, sys, graphicsRequirements);
}

XrResult
oxr_xrGetVulkanGraphicsRequirements2KHR(XrInstance instance,
                                        XrSystemId systemId,
                                        XrGraphicsRequirementsVulkan2KHR *graphicsRequirements)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetVulkanGraphicsRequirementsKHR");
	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, systemId, sys);
	/* XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR aliased to
	 * XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR */
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, graphicsRequirements, XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR);

	return oxr_vk_get_requirements(&log, sys, graphicsRequirements);
}

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrCreateVulkanInstanceKHR(XrInstance instance,
                              const XrVulkanInstanceCreateInfoKHR *createInfo,
                              VkInstance *vulkanInstance,
                              VkResult *vulkanResult)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrCreateVulkanInstanceKHR");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, createInfo, XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR);

	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, createInfo->systemId, sys);
	OXR_VERIFY_ARG_NOT_NULL(&log, createInfo->pfnGetInstanceProcAddr);
	OXR_VERIFY_ARG_ZERO(&log, createInfo->createFlags);

	OXR_VERIFY_ARG_NOT_NULL(&log, createInfo->pfnGetInstanceProcAddr);
	OXR_VERIFY_ARG_NOT_NULL(&log, createInfo->vulkanCreateInfo);

	// createInfo->vulkanAllocator can be NULL

	if (createInfo->vulkanCreateInfo->sType != VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE,
		                 "createInfo->vulkanCreateInfo->sType must be "
		                 "VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO");
	}

	return oxr_vk_create_vulkan_instance(&log, sys, createInfo, vulkanInstance, vulkanResult);
}

XrResult
oxr_xrCreateVulkanDeviceKHR(XrInstance instance,
                            const XrVulkanDeviceCreateInfoKHR *createInfo,
                            VkDevice *vulkanDevice,
                            VkResult *vulkanResult)
{
	struct oxr_instance *inst;
	struct oxr_logger log;

	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetVulkanGraphicsDeviceKHR");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, createInfo, XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR);

	OXR_VERIFY_SYSTEM_AND_GET(&log, inst, createInfo->systemId, sys);
	OXR_VERIFY_ARG_ZERO(&log, createInfo->createFlags);

	OXR_VERIFY_ARG_NOT_NULL(&log, createInfo->pfnGetInstanceProcAddr);
	OXR_VERIFY_ARG_NOT_NULL(&log, createInfo->vulkanCreateInfo);

	// VK_NULL_HANDLE is 0
	OXR_VERIFY_ARG_NOT_NULL(&log, createInfo->vulkanPhysicalDevice);

	OXR_VERIFY_ARG_NOT_NULL(&log, sys->vulkan_enable2_physical_device);
	OXR_VERIFY_ARG_NOT_NULL(&log, sys->vulkan_enable2_instance);

	if (sys->vulkan_enable2_physical_device != createInfo->vulkanPhysicalDevice) {
		return oxr_error(&log, XR_ERROR_HANDLE_INVALID,
		                 "createInfo->vulkanPhysicalDevice must be the device "
		                 "returned by xrGetVulkanGraphicsDeviceKHR");
	}

	// createInfo->vulkanAllocator can be NULL

	return oxr_vk_create_vulkan_device(&log, sys, createInfo, vulkanDevice, vulkanResult);
}
#endif
