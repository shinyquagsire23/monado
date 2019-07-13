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
		function = (PFN_xrVoidFunction)(ret);                          \
	}

/*!
 * @brief Helper define for generating that GetInstanceProcAddr function.
 */
#define ENTRY_ELSE_IF(funcName)                                                \
	else if (strcmp(name, #funcName) == 0)                                 \
	{                                                                      \
		PFN_##funcName ret = &oxr_##funcName;                          \
		function = (PFN_xrVoidFunction)(ret);                          \
	}

/*!
 * Handle a non-null instance pointer.
 */
static XrResult
handle_none_null(struct oxr_logger* log,
                 const char* name,
                 PFN_xrVoidFunction* out_function)
{
	PFN_xrVoidFunction function = NULL;

	ENTRY_IF(xrGetInstanceProcAddr)
	ENTRY_ELSE_IF(xrEnumerateApiLayerProperties)
	ENTRY_ELSE_IF(xrEnumerateInstanceExtensionProperties)
	ENTRY_ELSE_IF(xrCreateInstance)
	ENTRY_ELSE_IF(xrDestroyInstance)
	ENTRY_ELSE_IF(xrGetInstanceProperties)
	ENTRY_ELSE_IF(xrPollEvent)
	ENTRY_ELSE_IF(xrResultToString)
	ENTRY_ELSE_IF(xrStructureTypeToString)
	ENTRY_ELSE_IF(xrGetSystem)
	ENTRY_ELSE_IF(xrGetSystemProperties)
	ENTRY_ELSE_IF(xrEnumerateEnvironmentBlendModes)
	ENTRY_ELSE_IF(xrCreateSession)
	ENTRY_ELSE_IF(xrDestroySession)
	ENTRY_ELSE_IF(xrEnumerateReferenceSpaces)
	ENTRY_ELSE_IF(xrCreateReferenceSpace)
	ENTRY_ELSE_IF(xrGetReferenceSpaceBoundsRect)
	ENTRY_ELSE_IF(xrCreateActionSpace)
	ENTRY_ELSE_IF(xrLocateSpace)
	ENTRY_ELSE_IF(xrDestroySpace)
	ENTRY_ELSE_IF(xrEnumerateViewConfigurations)
	ENTRY_ELSE_IF(xrGetViewConfigurationProperties)
	ENTRY_ELSE_IF(xrEnumerateViewConfigurationViews)
	ENTRY_ELSE_IF(xrEnumerateSwapchainFormats)
	ENTRY_ELSE_IF(xrCreateSwapchain)
	ENTRY_ELSE_IF(xrDestroySwapchain)
	ENTRY_ELSE_IF(xrEnumerateSwapchainImages)
	ENTRY_ELSE_IF(xrAcquireSwapchainImage)
	ENTRY_ELSE_IF(xrWaitSwapchainImage)
	ENTRY_ELSE_IF(xrReleaseSwapchainImage)
	ENTRY_ELSE_IF(xrBeginSession)
	ENTRY_ELSE_IF(xrEndSession)
	ENTRY_ELSE_IF(xrWaitFrame)
	ENTRY_ELSE_IF(xrBeginFrame)
	ENTRY_ELSE_IF(xrEndFrame)
	ENTRY_ELSE_IF(xrRequestExitSession)
	ENTRY_ELSE_IF(xrLocateViews)
	ENTRY_ELSE_IF(xrStringToPath)
	ENTRY_ELSE_IF(xrPathToString)
	ENTRY_ELSE_IF(xrCreateActionSet)
	ENTRY_ELSE_IF(xrDestroyActionSet)
	ENTRY_ELSE_IF(xrCreateAction)
	ENTRY_ELSE_IF(xrDestroyAction)
	ENTRY_ELSE_IF(xrSuggestInteractionProfileBindings)
	ENTRY_ELSE_IF(xrAttachSessionActionSets)
	ENTRY_ELSE_IF(xrGetCurrentInteractionProfile)
	ENTRY_ELSE_IF(xrGetActionStateBoolean)
	ENTRY_ELSE_IF(xrGetActionStateFloat)
	ENTRY_ELSE_IF(xrGetActionStateVector2f)
	ENTRY_ELSE_IF(xrGetActionStatePose)
	ENTRY_ELSE_IF(xrSyncActions)
	ENTRY_ELSE_IF(xrEnumerateBoundSourcesForAction)
	ENTRY_ELSE_IF(xrGetInputSourceLocalizedName)
	ENTRY_ELSE_IF(xrApplyHapticFeedback)
	ENTRY_ELSE_IF(xrStopHapticFeedback)
#ifdef XR_KHR_visibility_mask
	ENTRY_ELSE_IF(xrGetVisibilityMaskKHR)
#endif
#ifdef XR_USE_TIMESPEC
	ENTRY_ELSE_IF(xrConvertTimespecTimeToTimeKHR)
	ENTRY_ELSE_IF(xrConvertTimeToTimespecTimeKHR)
#endif
#ifdef XR_EXT_performance_settings
	ENTRY_ELSE_IF(xrPerfSettingsSetPerformanceLevelEXT)
#endif
#ifdef XR_EXT_thermal_query
	ENTRY_ELSE_IF(xrThermalGetTemperatureTrendEXT)
#endif
#ifdef XR_EXT_debug_utils
	ENTRY_ELSE_IF(xrSetDebugUtilsObjectNameEXT)
	ENTRY_ELSE_IF(xrCreateDebugUtilsMessengerEXT)
	ENTRY_ELSE_IF(xrDestroyDebugUtilsMessengerEXT)
	ENTRY_ELSE_IF(xrSubmitDebugUtilsMessageEXT)
	ENTRY_ELSE_IF(xrSessionBeginDebugUtilsLabelRegionEXT)
	ENTRY_ELSE_IF(xrSessionEndDebugUtilsLabelRegionEXT)
	ENTRY_ELSE_IF(xrSessionInsertDebugUtilsLabelEXT)
#endif
#ifdef XR_USE_GRAPHICS_API_OPENGL
	ENTRY_ELSE_IF(xrGetOpenGLGraphicsRequirementsKHR)
#endif
#ifdef XR_USE_GRAPHICS_API_VULKAN
	ENTRY_ELSE_IF(xrGetVulkanInstanceExtensionsKHR)
	ENTRY_ELSE_IF(xrGetVulkanDeviceExtensionsKHR)
	ENTRY_ELSE_IF(xrGetVulkanGraphicsDeviceKHR)
	ENTRY_ELSE_IF(xrGetVulkanGraphicsRequirementsKHR)
#endif

	if (function == NULL) {
		return oxr_error(log, XR_ERROR_FUNCTION_UNSUPPORTED,
		                 "(name = \"%s\")", name);
	}

	*out_function = function;
	return XR_SUCCESS;
}

/*!
 * Special case a null instance pointer.
 */
static XrResult
handle_null(struct oxr_logger* log,
            const char* name,
            PFN_xrVoidFunction* out_function)
{
	PFN_xrVoidFunction function = NULL;

	ENTRY_IF(xrCreateInstance)
	ENTRY_ELSE_IF(xrEnumerateInstanceExtensionProperties)

	if (function == NULL) {
		return oxr_error(log, XR_ERROR_FUNCTION_UNSUPPORTED,
		                 "(name = \"%s\")", name);
	}

	*out_function = function;
	return XR_SUCCESS;
}

XrResult
oxr_xrGetInstanceProcAddr(XrInstance instance,
                          const char* name,
                          PFN_xrVoidFunction* function)
{
	if (instance == NULL) {
		struct oxr_logger log;
		oxr_log_init(&log, "xrGetInstanceProcAddr");

		return handle_null(&log, name, function);
	} else {
		struct oxr_instance* inst;
		struct oxr_logger log;
		OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst,
		                                 "xrGetInstanceProcAddr");

		return handle_none_null(&log, name, function);
	}
}
