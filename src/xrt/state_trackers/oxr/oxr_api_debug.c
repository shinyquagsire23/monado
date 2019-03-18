// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Debug messaging entrypoints for the OpenXR state tracker.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_api
 */

#include "oxr_objects.h"
#include "oxr_logger.h"

#include "oxr_api_funcs.h"
#include "oxr_api_verify.h"

#ifdef XR_EXT_debug_utils


XrResult
oxr_xrSetDebugUtilsObjectNameEXT(XrInstance instance,
                                 const XrDebugUtilsObjectNameInfoEXT* nameInfo)
{
	struct oxr_instance* inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst,
	                                 "xrSetDebugUtilsObjectNameEXT");

	return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " not implemented");
}

XrResult
oxr_xrCreateDebugUtilsMessengerEXT(
    XrInstance instance,
    const XrDebugUtilsMessengerCreateInfoEXT* createInfo,
    XrDebugUtilsMessengerEXT* messenger)
{
	struct oxr_instance* inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst,
	                                 "xrCreateDebugUtilsMessengerEXT");

	return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " not implemented");
}

XrResult
oxr_xrDestroyDebugUtilsMessengerEXT(XrDebugUtilsMessengerEXT messenger)
{
	struct oxr_debug_messenger* mssngr;
	struct oxr_logger log;
	OXR_VERIFY_MESSENGER_AND_INIT_LOG(&log, messenger, mssngr,
	                                  "xrDestroyDebugUtilsMessengerEXT");

	return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " not implemented");
}

XrResult
oxr_xrSubmitDebugUtilsMessageEXT(
    XrInstance instance,
    XrDebugUtilsMessageSeverityFlagsEXT messageSeverity,
    XrDebugUtilsMessageTypeFlagsEXT messageTypes,
    const XrDebugUtilsMessengerCallbackDataEXT* callbackData)
{
	struct oxr_instance* inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst,
	                                 "xrSubmitDebugUtilsMessageEXT");

	return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " not implemented");
}

XrResult
oxr_xrSessionBeginDebugUtilsLabelRegionEXT(
    XrSession session, const XrDebugUtilsLabelEXT* labelInfo)
{
	struct oxr_session* sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(
	    &log, session, sess, "xrSessionBeginDebugUtilsLabelRegionEXT");

	return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " not implemented");
}

XrResult
oxr_xrSessionEndDebugUtilsLabelRegionEXT(XrSession session)
{
	struct oxr_session* sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrSessionEndDebugUtilsLabelRegionEXT");

	return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " not implemented");
}

XrResult
oxr_xrSessionInsertDebugUtilsLabelEXT(XrSession session,
                                      const XrDebugUtilsLabelEXT* labelInfo)
{
	struct oxr_session* sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrSessionInsertDebugUtilsLabelEXT");

	return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " not implemented");
}

#endif
