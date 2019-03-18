// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Logging functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include <stdio.h>
#include <stdarg.h>

#include "xrt/xrt_compiler.h"
#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"


DEBUG_GET_ONCE_BOOL_OPTION(entrypoints, "OXR_DEBUG_ENTRYPOINTS", false)

static const char *
oxr_result_to_string(XrResult result);


void
oxr_log_init(struct oxr_logger *logger, const char *api_func_name)
{
	if (debug_get_bool_option_entrypoints()) {
		fprintf(stderr, "%s\n", api_func_name);
	}

	logger->inst = NULL;
	logger->api_func_name = api_func_name;
}

void
oxr_log_set_instance(struct oxr_logger *logger, struct oxr_instance *inst)
{
	logger->inst = inst;
}

void
oxr_log(struct oxr_logger *logger, const char *fmt, ...)
{
	if (logger->api_func_name != NULL) {
		fprintf(stderr, " in %s", logger->api_func_name);
	}

	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
}

void
oxr_warn(struct oxr_logger *logger, const char *fmt, ...)
{
	if (logger->api_func_name != NULL) {
		fprintf(stderr, "%s WARNING: ", logger->api_func_name);
	} else {
		fprintf(stderr, "WARNING: ");
	}

	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
}

XrResult
oxr_error(struct oxr_logger *logger, XrResult result, const char *fmt, ...)
{
	if (debug_get_bool_option_entrypoints()) {
		fprintf(stderr, "\t");
	}

	fprintf(stderr, "%s", oxr_result_to_string(result));

	if (logger->api_func_name != NULL) {
		fprintf(stderr, " in %s", logger->api_func_name);
	}

	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
	return result;
}

static const char *
oxr_result_to_string(XrResult result)
{
	// clang-format off
	switch (result) {
	case XR_SUCCESS: return "XR_SUCCESS";
	case XR_TIMEOUT_EXPIRED: return "XR_TIMEOUT_EXPIRED";
	case XR_SESSION_VISIBILITY_UNAVAILABLE: return "XR_SESSION_VISIBILITY_UNAVAILABLE";
	case XR_SESSION_LOSS_PENDING: return "XR_SESSION_LOSS_PENDING";
	case XR_EVENT_UNAVAILABLE: return "XR_EVENT_UNAVAILABLE";
	case XR_STATE_UNAVAILABLE: return "XR_STATE_UNAVAILABLE";
	case XR_STATE_TYPE_UNAVAILABLE: return "XR_STATE_TYPE_UNAVAILABLE";
	case XR_SPACE_BOUNDS_UNAVAILABLE: return "XR_SPACE_BOUNDS_UNAVAILABLE";
	case XR_SESSION_NOT_FOCUSED: return "XR_SESSION_NOT_FOCUSED";
	case XR_FRAME_DISCARDED: return "XR_FRAME_DISCARDED";
	case XR_ERROR_VALIDATION_FAILURE: return "XR_ERROR_VALIDATION_FAILURE";
	case XR_ERROR_RUNTIME_FAILURE: return "XR_ERROR_RUNTIME_FAILURE";
	case XR_ERROR_OUT_OF_MEMORY: return "XR_ERROR_OUT_OF_MEMORY";
	case XR_ERROR_RUNTIME_VERSION_INCOMPATIBLE: return "XR_ERROR_RUNTIME_VERSION_INCOMPATIBLE";
	case XR_ERROR_DRIVER_INCOMPATIBLE: return "XR_ERROR_DRIVER_INCOMPATIBLE";
	case XR_ERROR_INITIALIZATION_FAILED: return "XR_ERROR_INITIALIZATION_FAILED";
	case XR_ERROR_FUNCTION_UNSUPPORTED: return "XR_ERROR_FUNCTION_UNSUPPORTED";
	case XR_ERROR_FEATURE_UNSUPPORTED: return "XR_ERROR_FEATURE_UNSUPPORTED";
	case XR_ERROR_EXTENSION_NOT_PRESENT: return "XR_ERROR_EXTENSION_NOT_PRESENT";
	case XR_ERROR_LIMIT_REACHED: return "XR_ERROR_LIMIT_REACHED";
	case XR_ERROR_SIZE_INSUFFICIENT: return "XR_ERROR_SIZE_INSUFFICIENT";
	case XR_ERROR_HANDLE_INVALID: return "XR_ERROR_HANDLE_INVALID";
	case XR_ERROR_INSTANCE_LOST: return "XR_ERROR_INSTANCE_LOST";
	case XR_ERROR_SESSION_RUNNING: return "XR_ERROR_SESSION_RUNNING";
	case XR_ERROR_SESSION_NOT_RUNNING: return "XR_ERROR_SESSION_NOT_RUNNING";
	case XR_ERROR_SESSION_LOST: return "XR_ERROR_SESSION_LOST";
	case XR_ERROR_SYSTEM_INVALID: return "XR_ERROR_SYSTEM_INVALID";
	case XR_ERROR_PATH_INVALID: return "XR_ERROR_PATH_INVALID";
	case XR_ERROR_PATH_COUNT_EXCEEDED: return "XR_ERROR_PATH_COUNT_EXCEEDED";
	case XR_ERROR_PATH_FORMAT_INVALID: return "XR_ERROR_PATH_FORMAT_INVALID";
	case XR_ERROR_LAYER_INVALID: return "XR_ERROR_LAYER_INVALID";
	case XR_ERROR_LAYER_LIMIT_EXCEEDED: return "XR_ERROR_LAYER_LIMIT_EXCEEDED";
	case XR_ERROR_SWAPCHAIN_RECT_INVALID: return "XR_ERROR_SWAPCHAIN_RECT_INVALID";
	case XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED: return "XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED";
	case XR_ERROR_ACTION_TYPE_MISMATCH: return "XR_ERROR_ACTION_TYPE_MISMATCH";
	case XR_ERROR_REFERENCE_SPACE_UNSUPPORTED: return "XR_ERROR_REFERENCE_SPACE_UNSUPPORTED";
	case XR_ERROR_FILE_ACCESS_ERROR: return "XR_ERROR_FILE_ACCESS_ERROR";
	case XR_ERROR_FILE_CONTENTS_INVALID: return "XR_ERROR_FILE_CONTENTS_INVALID";
	case XR_ERROR_FORM_FACTOR_UNSUPPORTED: return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
	case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
	case XR_ERROR_API_LAYER_NOT_PRESENT: return "XR_ERROR_API_LAYER_NOT_PRESENT";
	case XR_ERROR_CALL_ORDER_INVALID: return "XR_ERROR_CALL_ORDER_INVALID";
	case XR_ERROR_GRAPHICS_DEVICE_INVALID: return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
	case XR_ERROR_POSE_INVALID: return "XR_ERROR_POSE_INVALID";
	case XR_ERROR_INDEX_OUT_OF_RANGE: return "XR_ERROR_INDEX_OUT_OF_RANGE";
	case XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED: return "XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED";
	case XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED: return "XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED";
	case XR_ERROR_BINDINGS_DUPLICATED: return "XR_ERROR_BINDINGS_DUPLICATED";
	case XR_ERROR_NAME_DUPLICATED: return "XR_ERROR_NAME_DUPLICATED";
	case XR_ERROR_NAME_INVALID: return "XR_ERROR_NAME_INVALID";
	case XR_ERROR_ANDROID_THREAD_SETTINGS_ID_INVALID_KHR: return "XR_ERROR_ANDROID_THREAD_SETTINGS_ID_INVALID_KHR";
	case XR_ERROR_ANDROID_THREAD_SETTINGS_FAILURE_KHR: return "XR_ERROR_ANDROID_THREAD_SETTINGS_FAILURE_KHR";
	case XR_ERROR_DEBUG_UTILS_MESSENGER_INVALID_EXT: return "XR_ERROR_DEBUG_UTILS_MESSENGER_INVALID_EXT";
	default: return "<UNKNOWN>";
	}
	// clang-format on
}
