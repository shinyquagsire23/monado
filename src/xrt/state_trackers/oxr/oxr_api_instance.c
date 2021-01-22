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

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_extension_support.h"
#include "oxr_chain.h"

#include "oxr_api_funcs.h"
#include "oxr_api_verify.h"

#include "openxr/openxr.h"
#include "openxr/openxr_reflection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


#define MAKE_EXTENSION_PROPERTIES(mixed_case, all_caps)                                                                \
	{XR_TYPE_EXTENSION_PROPERTIES, NULL, XR_##all_caps##_EXTENSION_NAME, XR_##mixed_case##_SPEC_VERSION},
static const XrExtensionProperties extension_properties[] = {OXR_EXTENSION_SUPPORT_GENERATE(MAKE_EXTENSION_PROPERTIES)};

XrResult
oxr_xrEnumerateInstanceExtensionProperties(const char *layerName,
                                           uint32_t propertyCapacityInput,
                                           uint32_t *propertyCountOutput,
                                           XrExtensionProperties *properties)
{
	struct oxr_logger log;
	oxr_log_init(&log, "xrEnumerateInstanceExtensionProperties");

	OXR_TWO_CALL_HELPER(&log, propertyCapacityInput, propertyCountOutput, properties,
	                    ARRAY_SIZE(extension_properties), extension_properties, XR_SUCCESS);
}

#ifdef XRT_OS_ANDROID
static XrResult
oxr_check_android_extensions(struct oxr_logger *log, const XrInstanceCreateInfo *createInfo)
{

	bool foundAndroidExtension = false;
	for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i) {
		if (strcmp(createInfo->enabledExtensionNames[i], XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) == 0) {
			foundAndroidExtension = true;
			break;
		}
	}
	if (!foundAndroidExtension) {
		return oxr_error(log, XR_ERROR_INITIALIZATION_FAILED,
		                 "(createInfo->enabledExtensionNames) "
		                 "Mandatory platform-specific "
		                 "extension"
		                 " " XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME " not specified");
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

XrResult
oxr_xrCreateInstance(const XrInstanceCreateInfo *createInfo, XrInstance *out_instance)
{
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

	/*
	 * Check that all extension names are recognized, so oxr_instance_create
	 * doesn't need to check for bad extension names.
	 */
#define CHECK_EXT_NAME(mixed_case, all_caps)                                                                           \
	if (strcmp(createInfo->enabledExtensionNames[i], XR_##all_caps##_EXTENSION_NAME) == 0) {                       \
		continue;                                                                                              \
	}
	for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i) {
		OXR_EXTENSION_SUPPORT_GENERATE(CHECK_EXT_NAME)

		return oxr_error(&log, XR_ERROR_EXTENSION_NOT_PRESENT,
		                 "(createInfo->enabledExtensionNames[%d]) "
		                 "Unrecognized extension name",
		                 i);
	}


#ifdef XRT_OS_ANDROID
	ret = oxr_check_android_extensions(&log, createInfo);
	if (ret != XR_SUCCESS) {
		return ret;
	}
#endif
	struct oxr_instance *inst = NULL;

	ret = oxr_instance_create(&log, createInfo, &inst);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*out_instance = oxr_instance_to_openxr(inst);

	return XR_SUCCESS;
}

XrResult
oxr_xrDestroyInstance(XrInstance instance)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrDestroyInstance");

	return oxr_handle_destroy(&log, &inst->handle);
}

XrResult
oxr_xrGetInstanceProperties(XrInstance instance, XrInstanceProperties *instanceProperties)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrGetInstanceProperties");

	return oxr_instance_get_properties(&log, inst, instanceProperties);
}

XrResult
oxr_xrPollEvent(XrInstance instance, XrEventDataBuffer *eventData)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrPollEvent");
	OXR_VERIFY_ARG_NOT_NULL(&log, eventData);

	return oxr_poll_event(&log, inst, eventData);
}

XrResult
oxr_xrResultToString(XrInstance instance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE])
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrResultToString");

#define MAKE_RESULT_CASE(VAL, _)                                                                                       \
	case VAL: strncpy(buffer, #VAL, XR_MAX_RESULT_STRING_SIZE); break;
	switch (value) {
		XR_LIST_ENUM_XrResult(MAKE_RESULT_CASE);
	default:
		snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XR_UNKNOWN_%s_%d", value < 0 ? "FAILURE" : "SUCCESS",
		         value);
	}
	buffer[XR_MAX_RESULT_STRING_SIZE - 1] = '\0';

	return XR_SUCCESS;
}

XrResult
oxr_xrStructureTypeToString(XrInstance instance, XrStructureType value, char buffer[XR_MAX_STRUCTURE_NAME_SIZE])
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrStructureTypeToString");

#define MAKE_TYPE_CASE(VAL, _)                                                                                         \
	case VAL: strncpy(buffer, #VAL, XR_MAX_RESULT_STRING_SIZE); break;
	switch (value) {
		XR_LIST_ENUM_XrStructureType(MAKE_TYPE_CASE);
	default: snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XR_UNKNOWN_STRUCTURE_TYPE_%d", value);
	}
	buffer[XR_MAX_RESULT_STRING_SIZE - 1] = '\0';

	return XR_SUCCESS;
}

XrResult
oxr_xrStringToPath(XrInstance instance, const char *pathString, XrPath *out_path)
{
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

XrResult
oxr_xrPathToString(
    XrInstance instance, XrPath path, uint32_t bufferCapacityInput, uint32_t *bufferCountOutput, char *buffer)
{
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

	return XR_SUCCESS;
}

// ---- XR_KHR_convert_timespec_time extension
#ifdef XR_USE_TIMESPEC
XrResult
oxr_xrConvertTimespecTimeToTimeKHR(XrInstance instance, const struct timespec *timespecTime, XrTime *time)
{
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

XrResult
oxr_xrConvertTimeToTimespecTimeKHR(XrInstance instance, XrTime time, struct timespec *timespecTime)
{
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

#endif // XR_USE_TIMESPEC
