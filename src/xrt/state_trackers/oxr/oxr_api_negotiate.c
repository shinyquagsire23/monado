// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  File for negotiating with the loader.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_api
 */


#include <stdio.h>
#include <string.h>

#include "xrt/xrt_compiler.h"
#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"

#include "oxr_api_funcs.h"
#include "oxr_api_verify.h"



DEBUG_GET_ONCE_BOOL_OPTION(negotiate, "OXR_DEBUG_NEGOTIATE", false)

#define PRINT_NEGOTIATE(...)                                                   \
	do {                                                                   \
		if (debug_get_bool_option_negotiate()) {                       \
			fprintf(stderr, __VA_ARGS__);                          \
		}                                                              \
	} while (false)


XrResult
xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo* loaderInfo,
                                  XrNegotiateRuntimeRequest* runtimeRequest)
{
	PRINT_NEGOTIATE("xrNegotiateLoaderRuntimeInterface\n");

	// Make sure that we understand the structs passed to this function.
	if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
	    loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
	    loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)) {
		PRINT_NEGOTIATE("\tloaderInfo bad!\n");
		return XR_ERROR_INITIALIZATION_FAILED;
	}

	// Make sure that we understand the structs passed to this function.
	if (runtimeRequest->structType !=
	        XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST ||
	    runtimeRequest->structVersion !=
	        XR_CURRENT_LOADER_RUNTIME_VERSION ||
	    runtimeRequest->structSize != sizeof(XrNegotiateRuntimeRequest)) {
		PRINT_NEGOTIATE("\truntimeRequest bad!\n");
		return XR_ERROR_INITIALIZATION_FAILED;
	}

	// TODO: properly define what we support
	uint16_t supported_major = XR_VERSION_MAJOR(XR_CURRENT_API_VERSION);

	uint32_t requested_min_major = loaderInfo->minInterfaceVersion;
	uint32_t requested_max_major = loaderInfo->maxInterfaceVersion;

	if (supported_major > requested_max_major ||
	    supported_major < requested_min_major) {
		PRINT_NEGOTIATE(
		    "\tXRT - OpenXR doesn't support requested version %d <= "
		    "%d <= %d\n",
		    requested_min_major, supported_major, requested_max_major);
		return XR_ERROR_INITIALIZATION_FAILED;
	}

	runtimeRequest->getInstanceProcAddr = oxr_xrGetInstanceProcAddr;
	runtimeRequest->runtimeInterfaceVersion =
	    XR_CURRENT_LOADER_RUNTIME_VERSION;
	runtimeRequest->runtimeApiVersion = XR_CURRENT_API_VERSION;

	PRINT_NEGOTIATE("\tall ok!\n");

	return XR_SUCCESS;
}

XrResult
oxr_xrEnumerateApiLayerProperties(uint32_t propertyCapacityInput,
                                  uint32_t* propertyCountOutput,
                                  XrApiLayerProperties* properties)
{
	struct oxr_logger log;
	oxr_log_init(&log, "xrEnumerateApiLayerProperties");

	/* We have no layers inbuilt. */
	if (propertyCountOutput != NULL) {
		*propertyCountOutput = 0;
	}

	return XR_SUCCESS;
}

/*!
 * @brief Helper define for generating that GetInstanceProcAddr function.
 */
#define ENTRY_IF(funcName)                                                     \
	if (strcmp(name, #funcName) == 0) {                                    \
		PFN_##funcName ret = &oxr_##funcName;                          \
		*out_function = (PFN_xrVoidFunction)(ret);                     \
		return XR_SUCCESS;                                             \
	}

/*!
 * @brief Helper define for generating that GetInstanceProcAddr function: checks
 * the extra condition to find out if the extension is enabled
 */
#define ENTRY_IF_EXT(funcName, extraCondition)                                 \
	if (strcmp(name, #funcName) == 0) {                                    \
		if (extraCondition) {                                          \
			PFN_##funcName ret = &oxr_##funcName;                  \
			*out_function = (PFN_xrVoidFunction)(ret);             \
			return XR_SUCCESS;                                     \
		}                                                              \
		return oxr_error(                                              \
		    log, XR_ERROR_FUNCTION_UNSUPPORTED,                        \
		    "(name = \"%s\") Required extension not enabled", name);   \
	}

/*!
 * Handle a non-null instance pointer.
 */
static XrResult
handle_non_null(struct oxr_instance* inst,
                struct oxr_logger* log,
                const char* name,
                PFN_xrVoidFunction* out_function)
{
	ENTRY_IF(xrGetInstanceProcAddr)
	ENTRY_IF(xrEnumerateInstanceExtensionProperties)
	ENTRY_IF(xrCreateInstance)
	ENTRY_IF(xrDestroyInstance)
	ENTRY_IF(xrGetInstanceProperties)
	ENTRY_IF(xrPollEvent)
	ENTRY_IF(xrResultToString)
	ENTRY_IF(xrStructureTypeToString)
	ENTRY_IF(xrGetSystem)
	ENTRY_IF(xrGetSystemProperties)
	ENTRY_IF(xrEnumerateEnvironmentBlendModes)
	ENTRY_IF(xrCreateSession)
	ENTRY_IF(xrDestroySession)
	ENTRY_IF(xrEnumerateReferenceSpaces)
	ENTRY_IF(xrCreateReferenceSpace)
	ENTRY_IF(xrGetReferenceSpaceBoundsRect)
	ENTRY_IF(xrCreateActionSpace)
	ENTRY_IF(xrLocateSpace)
	ENTRY_IF(xrDestroySpace)
	ENTRY_IF(xrEnumerateViewConfigurations)
	ENTRY_IF(xrGetViewConfigurationProperties)
	ENTRY_IF(xrEnumerateViewConfigurationViews)
	ENTRY_IF(xrEnumerateSwapchainFormats)
	ENTRY_IF(xrCreateSwapchain)
	ENTRY_IF(xrDestroySwapchain)
	ENTRY_IF(xrEnumerateSwapchainImages)
	ENTRY_IF(xrAcquireSwapchainImage)
	ENTRY_IF(xrWaitSwapchainImage)
	ENTRY_IF(xrReleaseSwapchainImage)
	ENTRY_IF(xrBeginSession)
	ENTRY_IF(xrEndSession)
	ENTRY_IF(xrWaitFrame)
	ENTRY_IF(xrBeginFrame)
	ENTRY_IF(xrEndFrame)
	ENTRY_IF(xrRequestExitSession)
	ENTRY_IF(xrLocateViews)
	ENTRY_IF(xrStringToPath)
	ENTRY_IF(xrPathToString)
	ENTRY_IF(xrCreateActionSet)
	ENTRY_IF(xrDestroyActionSet)
	ENTRY_IF(xrCreateAction)
	ENTRY_IF(xrDestroyAction)
	ENTRY_IF(xrSuggestInteractionProfileBindings)
	ENTRY_IF(xrAttachSessionActionSets)
	ENTRY_IF(xrGetCurrentInteractionProfile)
	ENTRY_IF(xrGetActionStateBoolean)
	ENTRY_IF(xrGetActionStateFloat)
	ENTRY_IF(xrGetActionStateVector2f)
	ENTRY_IF(xrGetActionStatePose)
	ENTRY_IF(xrSyncActions)
	ENTRY_IF(xrEnumerateBoundSourcesForAction)
	ENTRY_IF(xrGetInputSourceLocalizedName)
	ENTRY_IF(xrApplyHapticFeedback)
	ENTRY_IF(xrStopHapticFeedback)

	//! @todo all extension functions should use ENTRY_IF_EXT !

#ifdef XR_KHR_visibility_mask
	ENTRY_IF(xrGetVisibilityMaskKHR)
#endif
#ifdef XR_USE_TIMESPEC
	ENTRY_IF(xrConvertTimespecTimeToTimeKHR)
	ENTRY_IF(xrConvertTimeToTimespecTimeKHR)
#endif
#ifdef XR_EXT_performance_settings
	ENTRY_IF(xrPerfSettingsSetPerformanceLevelEXT)
#endif
#ifdef XR_EXT_thermal_query
	ENTRY_IF(xrThermalGetTemperatureTrendEXT)
#endif
#ifdef XR_EXT_debug_utils
	ENTRY_IF(xrSetDebugUtilsObjectNameEXT)
	ENTRY_IF(xrCreateDebugUtilsMessengerEXT)
	ENTRY_IF(xrDestroyDebugUtilsMessengerEXT)
	ENTRY_IF(xrSubmitDebugUtilsMessageEXT)
	ENTRY_IF(xrSessionBeginDebugUtilsLabelRegionEXT)
	ENTRY_IF(xrSessionEndDebugUtilsLabelRegionEXT)
	ENTRY_IF(xrSessionInsertDebugUtilsLabelEXT)
#endif
#ifdef XR_USE_GRAPHICS_API_OPENGL
	ENTRY_IF_EXT(xrGetOpenGLGraphicsRequirementsKHR, inst->opengl_enable)
#endif
#ifdef XR_USE_GRAPHICS_API_VULKAN
	ENTRY_IF_EXT(xrGetVulkanInstanceExtensionsKHR, inst->vulkan_enable)
	ENTRY_IF_EXT(xrGetVulkanDeviceExtensionsKHR, inst->vulkan_enable)
	ENTRY_IF_EXT(xrGetVulkanGraphicsDeviceKHR, inst->vulkan_enable)
	ENTRY_IF_EXT(xrGetVulkanGraphicsRequirementsKHR, inst->vulkan_enable)
#endif

	/*
	 * Not logging here because there's no need to loudly advertise
	 * which extensions the loader knows about (it calls this on
	 * every known function) that we don't implement.
	 */
	return XR_ERROR_FUNCTION_UNSUPPORTED;
}

/*!
 * Special case a null instance pointer.
 */
static XrResult
handle_null(struct oxr_logger* log,
            const char* name,
            PFN_xrVoidFunction* out_function)
{
	ENTRY_IF(xrCreateInstance)
	ENTRY_IF(xrEnumerateInstanceExtensionProperties)
	ENTRY_IF(xrEnumerateApiLayerProperties)

	/*
	 * This is fine to log, since there should not be other
	 * null-instance calls.
	 */
	return oxr_error(log, XR_ERROR_FUNCTION_UNSUPPORTED, "(name = \"%s\")",
	                 name);
}

XrResult
oxr_xrGetInstanceProcAddr(XrInstance instance,
                          const char* name,
                          PFN_xrVoidFunction* function)
{
	struct oxr_logger log;

	// We need to set this unconditionally, per the spec.
	*function = NULL;

	if (instance == NULL) {
		oxr_log_init(&log, "xrGetInstanceProcAddr");
		return handle_null(&log, name, function);
	}

	struct oxr_instance* inst;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst,
	                                 "xrGetInstanceProcAddr");
	return handle_non_null(inst, &log, name, function);
}
