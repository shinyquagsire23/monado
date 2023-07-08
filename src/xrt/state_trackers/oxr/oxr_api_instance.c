// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds instance related entrypoints.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_api
 */

#include "xrt/xrt_compiler.h"

#include "util/u_debug.h"
#include "util/u_trace_marker.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_extension_support.h"
#include "oxr_chain.h"

#include "oxr_api_funcs.h"
#include "oxr_api_verify.h"


#ifdef XRT_OS_ANDROID
#include "android/android_globals.h"
#endif

#include "openxr/openxr.h"
#include "openxr/openxr_reflection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


#define MAKE_EXTENSION_PROPERTIES(mixed_case, all_caps)                                                                \
	{XR_TYPE_EXTENSION_PROPERTIES, NULL, XR_##all_caps##_EXTENSION_NAME, XR_##mixed_case##_SPEC_VERSION},
static const XrExtensionProperties extension_properties[] = {OXR_EXTENSION_SUPPORT_GENERATE(MAKE_EXTENSION_PROPERTIES)};

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrEnumerateInstanceExtensionProperties(const char *layerName,
                                           uint32_t propertyCapacityInput,
                                           uint32_t *propertyCountOutput,
                                           XrExtensionProperties *properties)
{
	OXR_TRACE_MARKER();

	struct oxr_logger log;
	oxr_log_init(&log, "xrEnumerateInstanceExtensionProperties");

	OXR_TWO_CALL_HELPER(&log, propertyCapacityInput, propertyCountOutput, properties,
	                    ARRAY_SIZE(extension_properties), extension_properties, XR_SUCCESS);
}

#ifdef OXR_HAVE_KHR_loader_init
XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrInitializeLoaderKHR(const XrLoaderInitInfoBaseHeaderKHR *loaderInitInfo)
{
	struct oxr_logger log;
	oxr_log_init(&log, "oxr_xrInitializeLoaderKHR");


	oxr_log(&log, "Loader forwarded call to xrInitializeLoaderKHR.");
#ifdef XRT_OS_ANDROID
	const XrLoaderInitInfoAndroidKHR *initInfoAndroid =
	    OXR_GET_INPUT_FROM_CHAIN(loaderInitInfo, XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR, XrLoaderInitInfoAndroidKHR);
	if (initInfoAndroid == NULL) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE,
		                 "(loaderInitInfo) "
		                 "Did not find XrLoaderInitInfoAndroidKHR");
	}
	if (initInfoAndroid->applicationVM == NULL) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE,
		                 "(initInfoAndroid->applicationVM) "
		                 "applicationVM must be populated");
	}
	if (initInfoAndroid->applicationContext == NULL) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE,
		                 "(initInfoAndroid->applicationContext) "
		                 "applicationContext must be populated");
	}
	//! @todo check that applicationContext is in fact an Activity.
	android_globals_store_vm_and_context(initInfoAndroid->applicationVM, initInfoAndroid->applicationContext);

#endif // XRT_OS_ANDROID
	return XR_SUCCESS;
}
#endif // OXR_HAVE_KHR_loader_init


#ifdef XRT_OS_ANDROID
static XrResult
oxr_check_android_extensions(struct oxr_logger *log,
                             const XrInstanceCreateInfo *createInfo,
                             const struct oxr_extension_status *extensions)
{
	// We need the XR_KHR_android_create_instance extension.
	if (!extensions->KHR_android_create_instance) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 "(createInfo->enabledExtensionNames) "
		                 "Mandatory platform-specific extension " XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME
		                 " not specified");
	}

	{
		// Verify that it exists and is populated.
		XrInstanceCreateInfoAndroidKHR const *createInfoAndroid = OXR_GET_INPUT_FROM_CHAIN(
		    createInfo, XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR, XrInstanceCreateInfoAndroidKHR);
		if (createInfoAndroid == NULL) {
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
			                 "(createInfo->next...) "
			                 "Did not find XrInstanceCreateInfoAndroidKHR in "
			                 "chain");
		}
		if (createInfoAndroid->applicationVM == NULL) {
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
			                 "(createInfo->next...->applicationVM) "
			                 "applicationVM must be populated");
		}
		if (createInfoAndroid->applicationActivity == NULL) {
			return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
			                 "(createInfo->next...->applicationActivity) "
			                 "applicationActivity must be populated");
		}
	}
	return XR_SUCCESS;
}
#endif // XRT_OS_ANDROID

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrCreateInstance(const XrInstanceCreateInfo *createInfo, XrInstance *out_instance)
{
	OXR_TRACE_MARKER();

	XrResult ret;
	struct oxr_logger log;
	oxr_log_init(&log, "xrCreateInstance");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, createInfo, XR_TYPE_INSTANCE_CREATE_INFO);
	const uint32_t major = XR_VERSION_MAJOR(XR_CURRENT_API_VERSION);
	const uint32_t minor = XR_VERSION_MINOR(XR_CURRENT_API_VERSION);
	const uint32_t patch = XR_VERSION_PATCH(XR_CURRENT_API_VERSION);
	(void)patch; // Not used for now.


	if (createInfo->applicationInfo.apiVersion < XR_MAKE_VERSION(major, minor, 0)) {
		return oxr_error(&log, XR_ERROR_API_VERSION_UNSUPPORTED,
		                 "(createInfo->applicationInfo.apiVersion) "
		                 "Cannot satisfy request for version less than %d.%d.%d",
		                 major, minor, 0);
	}

	/*
	 * This is a slight fib, to let us approximately run things between 1.0
	 * and 2.0
	 */
	if (createInfo->applicationInfo.apiVersion >= XR_MAKE_VERSION(2, 0, 0)) {
		return oxr_error(&log, XR_ERROR_API_VERSION_UNSUPPORTED,
		                 "(createInfo->applicationInfo.apiVersion) "
		                 "Cannot satisfy request for version: too high");
	}

	// To be passed into verify and instance creation.
	struct oxr_extension_status extensions;
	U_ZERO(&extensions);

	/*
	 * Check that all extension names are recognized, so oxr_instance_create
	 * doesn't need to check for bad extension names.
	 *
	 * Also fill out the oxr_extension_status struct at the same time.
	 */
#define CHECK_EXT_NAME(mixed_case, all_caps)                                                                           \
	if (strcmp(createInfo->enabledExtensionNames[i], XR_##all_caps##_EXTENSION_NAME) == 0) {                       \
		extensions.mixed_case = true;                                                                          \
		continue;                                                                                              \
	}
	for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i) {
		OXR_EXTENSION_SUPPORT_GENERATE(CHECK_EXT_NAME)

		return oxr_error(&log, XR_ERROR_EXTENSION_NOT_PRESENT,
		                 "(createInfo->enabledExtensionNames[%d]) Unrecognized extension name '%s'", i,
		                 createInfo->enabledExtensionNames[i]);
	}

	ret = oxr_verify_extensions(&log, &extensions);
	if (ret != XR_SUCCESS) {
		return ret;
	}

#ifdef XRT_OS_ANDROID
	ret = oxr_check_android_extensions(&log, createInfo, &extensions);
	if (ret != XR_SUCCESS) {
		return ret;
	}
#endif
	struct oxr_instance *inst = NULL;

	ret = oxr_instance_create(&log, createInfo, &extensions, &inst);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*out_instance = oxr_instance_to_openxr(inst);

	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrDestroyInstance(XrInstance instance)
{
	OXR_TRACE_MARKER();

	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrDestroyInstance");

	return oxr_handle_destroy(&log, &inst->handle);
}

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrGetInstanceProperties(XrInstance instance, XrInstanceProperties *instanceProperties)
{
	OXR_TRACE_MARKER();

	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetInstanceProperties");

	return oxr_instance_get_properties(&log, inst, instanceProperties);
}

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrPollEvent(XrInstance instance, XrEventDataBuffer *eventData)
{
	OXR_TRACE_MARKER();

	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrPollEvent");
	OXR_VERIFY_ARG_NOT_NULL(&log, eventData);

	return oxr_poll_event(&log, inst, eventData);
}

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrResultToString(XrInstance instance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE])
{
	OXR_TRACE_MARKER();

	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrResultToString");

#define MAKE_RESULT_CASE(VAL, _)                                                                                       \
	case VAL: snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, #VAL); break;

	switch (value) {
		XR_LIST_ENUM_XrResult(MAKE_RESULT_CASE);
	default:
		snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XR_UNKNOWN_%s_%d", value < 0 ? "FAILURE" : "SUCCESS",
		         value);
	}
	// The function snprintf always null terminates.

	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrStructureTypeToString(XrInstance instance, XrStructureType value, char buffer[XR_MAX_STRUCTURE_NAME_SIZE])
{
	OXR_TRACE_MARKER();

	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrStructureTypeToString");

#define MAKE_TYPE_CASE(VAL, _)                                                                                         \
	case VAL: snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, #VAL); break;

	switch (value) {
		XR_LIST_ENUM_XrStructureType(MAKE_TYPE_CASE);
	default: snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XR_UNKNOWN_STRUCTURE_TYPE_%d", value);
	}
	// The function snprintf always null terminates.

	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrStringToPath(XrInstance instance, const char *pathString, XrPath *out_path)
{
	OXR_TRACE_MARKER();

	struct oxr_instance *inst;
	struct oxr_logger log;
	XrResult ret;
	XrPath path;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrStringToPath");

	ret = oxr_verify_full_path_c(&log, pathString, "pathString");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	ret = oxr_path_get_or_create(&log, inst, pathString, strlen(pathString), &path);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*out_path = path;

	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrPathToString(
    XrInstance instance, XrPath path, uint32_t bufferCapacityInput, uint32_t *bufferCountOutput, char *buffer)
{
	OXR_TRACE_MARKER();

	struct oxr_instance *inst;
	struct oxr_logger log;
	const char *str;
	size_t length;
	XrResult ret;

	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrPathToString");
	if (path == XR_NULL_PATH) {
		return oxr_error(&log, XR_ERROR_PATH_INVALID, "(path == XR_NULL_PATH)");
	}

	ret = oxr_path_get_string(&log, inst, path, &str, &length);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	// Length is the number of valid characters, not including the
	// null termination character (but a extra null byte is always
	// reserved).
	OXR_TWO_CALL_HELPER(&log, bufferCapacityInput, bufferCountOutput, buffer, length + 1, str, XR_SUCCESS);
}

// ---- XR_KHR_convert_timespec_time extension
#ifdef OXR_HAVE_KHR_convert_timespec_time
XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrConvertTimespecTimeToTimeKHR(XrInstance instance, const struct timespec *timespecTime, XrTime *time)
{
	OXR_TRACE_MARKER();

	//! @todo do we need to check and see if this extension was
	//! enabled first?
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrConvertTimespecTimeToTimeKHR");
	OXR_VERIFY_EXTENSION(&log, inst, KHR_convert_timespec_time);
	OXR_VERIFY_ARG_NOT_NULL(&log, timespecTime);
	OXR_VERIFY_ARG_NOT_NULL(&log, time);
	return oxr_instance_convert_timespec_to_time(&log, inst, timespecTime, time);
}

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrConvertTimeToTimespecTimeKHR(XrInstance instance, XrTime time, struct timespec *timespecTime)
{
	OXR_TRACE_MARKER();

	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrConvertTimeToTimespecTimeKHR");
	OXR_VERIFY_EXTENSION(&log, inst, KHR_convert_timespec_time);
	OXR_VERIFY_ARG_NOT_NULL(&log, timespecTime);

	if (time <= (XrTime)0) {
		return oxr_error(&log, XR_ERROR_TIME_INVALID, "(time == %" PRIi64 ") is not a valid time.", time);
	}

	return oxr_instance_convert_time_to_timespec(&log, inst, time, timespecTime);
}

#endif // OXR_HAVE_KHR_convert_timespec_time

// ---- XR_KHR_win32_convert_performance_counter_time extension
#ifdef XR_USE_PLATFORM_WIN32
XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrConvertWin32PerformanceCounterToTimeKHR(XrInstance instance,
                                              const LARGE_INTEGER *performanceCounter,
                                              XrTime *time)
{
	OXR_TRACE_MARKER();

	//! @todo do we need to check and see if this extension was
	//! enabled first?
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrConvertWin32PerformanceCounterToTimeKHR");
	OXR_VERIFY_EXTENSION(&log, inst, KHR_win32_convert_performance_counter_time);
	OXR_VERIFY_ARG_NOT_NULL(&log, performanceCounter);
	OXR_VERIFY_ARG_NOT_NULL(&log, time);

	if (performanceCounter->QuadPart <= 0) {
		return oxr_error(&log, XR_ERROR_TIME_INVALID,
		                 "(time == %" PRIi64 ") is not a valid performance counter time.",
		                 performanceCounter->QuadPart);
	}

	return oxr_instance_convert_win32perfcounter_to_time(&log, inst, performanceCounter, time);
}

XRAPI_ATTR XrResult XRAPI_CALL
oxr_xrConvertTimeToWin32PerformanceCounterKHR(XrInstance instance, XrTime time, LARGE_INTEGER *performanceCounter)
{
	OXR_TRACE_MARKER();

	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrConvertTimeToWin32PerformanceCounterKHR");
	OXR_VERIFY_EXTENSION(&log, inst, KHR_win32_convert_performance_counter_time);
	OXR_VERIFY_ARG_NOT_NULL(&log, performanceCounter);

	if (time <= (XrTime)0) {
		return oxr_error(&log, XR_ERROR_TIME_INVALID, "(time == %" PRIi64 ") is not a valid time.", time);
	}

	return oxr_instance_convert_time_to_win32perfcounter(&log, inst, time, performanceCounter);
}

#endif // XR_USE_PLATFORM_WIN32
