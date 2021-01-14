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



XrResult
oxr_xrSetDebugUtilsObjectNameEXT(XrInstance instance, const XrDebugUtilsObjectNameInfoEXT *nameInfo)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrSetDebugUtilsObjectNameEXT");
	OXR_VERIFY_EXTENSION(&log, inst, EXT_debug_utils);
	return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " not implemented");
}

XrResult
oxr_xrCreateDebugUtilsMessengerEXT(XrInstance instance,
                                   const XrDebugUtilsMessengerCreateInfoEXT *createInfo,
                                   XrDebugUtilsMessengerEXT *messenger)
{
	struct oxr_instance *inst;
	struct oxr_debug_messenger *mssngr;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrCreateDebugUtilsMessengerEXT");
	OXR_VERIFY_EXTENSION(&log, inst, EXT_debug_utils);

	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, createInfo, XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
	OXR_VERIFY_ARG_NOT_NULL(&log, messenger);

	XrResult ret = oxr_create_messenger(&log, inst, createInfo, &mssngr);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*messenger = oxr_messenger_to_openxr(mssngr);

	return XR_SUCCESS;
}

XrResult
oxr_xrDestroyDebugUtilsMessengerEXT(XrDebugUtilsMessengerEXT messenger)
{
	struct oxr_debug_messenger *mssngr;
	struct oxr_logger log;
	OXR_VERIFY_MESSENGER_AND_INIT_LOG(&log, messenger, mssngr, "xrDestroyDebugUtilsMessengerEXT");
	OXR_VERIFY_EXTENSION(&log, mssngr->inst, EXT_debug_utils);

	return oxr_handle_destroy(&log, &mssngr->handle);
}

XrResult
oxr_xrSubmitDebugUtilsMessageEXT(XrInstance instance,
                                 XrDebugUtilsMessageSeverityFlagsEXT messageSeverity,
                                 XrDebugUtilsMessageTypeFlagsEXT messageTypes,
                                 const XrDebugUtilsMessengerCallbackDataEXT *callbackData)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrSubmitDebugUtilsMessageEXT");
	OXR_VERIFY_EXTENSION(&log, inst, EXT_debug_utils);

	oxr_warn(&log, " not fully implemented");
	return XR_SUCCESS;
}

XrResult
oxr_xrSessionBeginDebugUtilsLabelRegionEXT(XrSession session, const XrDebugUtilsLabelEXT *labelInfo)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrSessionBeginDebugUtilsLabelRegionEXT");
	OXR_VERIFY_EXTENSION(&log, sess->sys->inst, EXT_debug_utils);

	return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " not implemented");
}

XrResult
oxr_xrSessionEndDebugUtilsLabelRegionEXT(XrSession session)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrSessionEndDebugUtilsLabelRegionEXT");
	OXR_VERIFY_EXTENSION(&log, sess->sys->inst, EXT_debug_utils);

	return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " not implemented");
}

XrResult
oxr_xrSessionInsertDebugUtilsLabelEXT(XrSession session, const XrDebugUtilsLabelEXT *labelInfo)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrSessionInsertDebugUtilsLabelEXT");
	OXR_VERIFY_EXTENSION(&log, sess->sys->inst, EXT_debug_utils);

	return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " not implemented");
}
