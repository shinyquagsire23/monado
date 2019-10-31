// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Session entrypoints for the OpenXR state tracker.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_api
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xrt/xrt_compiler.h"

#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"

#include "oxr_api_funcs.h"
#include "oxr_api_verify.h"



XrResult
oxr_xrCreateSession(XrInstance instance,
                    const XrSessionCreateInfo *createInfo,
                    XrSession *out_session)
{
	XrResult ret;
	struct oxr_instance *inst;
	struct oxr_session *sess, **link;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst,
	                                 "xrCreateSession");

	ret = oxr_verify_XrSessionCreateInfo(&log, inst, createInfo);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	ret = oxr_session_create(&log, &inst->system, createInfo, &sess);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*out_session = oxr_session_to_openxr(sess);

	/* Add to session list */
	link = &inst->sessions;
	while (*link) {
		link = &(*link)->next;
	}
	*link = sess;

	return XR_SUCCESS;
}

XrResult
oxr_xrDestroySession(XrSession session)
{
	struct oxr_session *sess, **link;
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrDestroySession");

	/* Remove from session list */
	inst = sess->sys->inst;
	link = &inst->sessions;
	while (*link != sess) {
		link = &(*link)->next;
	}
	*link = sess->next;

	return oxr_handle_destroy(&log, &sess->handle);
}

XrResult
oxr_xrBeginSession(XrSession session, const XrSessionBeginInfo *beginInfo)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrBeginSession");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, beginInfo,
	                                 XR_TYPE_SESSION_BEGIN_INFO);

	return oxr_session_begin(&log, sess, beginInfo);
}

XrResult
oxr_xrEndSession(XrSession session)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrEndSession");

	return oxr_session_end(&log, sess);
}

XrResult
oxr_xrWaitFrame(XrSession session,
                const XrFrameWaitInfo *frameWaitInfo,
                XrFrameState *frameState)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrWaitFrame");
	OXR_VERIFY_ARG_TYPE_CAN_BE_NULL(&log, frameWaitInfo,
	                                XR_TYPE_FRAME_WAIT_INFO);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, frameState, XR_TYPE_FRAME_STATE);
	OXR_VERIFY_ARG_NOT_NULL(&log, frameState);

	return oxr_session_frame_wait(&log, sess, frameState);
}

XrResult
oxr_xrBeginFrame(XrSession session, const XrFrameBeginInfo *frameBeginInfo)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrBeginFrame");
	// NULL explicitly allowed here because it's a basically empty struct.
	OXR_VERIFY_ARG_TYPE_CAN_BE_NULL(&log, frameBeginInfo,
	                                XR_TYPE_FRAME_BEGIN_INFO);

	return oxr_session_frame_begin(&log, sess);
}

XrResult
oxr_xrEndFrame(XrSession session, const XrFrameEndInfo *frameEndInfo)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrEndFrame");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, frameEndInfo,
	                                 XR_TYPE_FRAME_END_INFO);

	return oxr_session_frame_end(&log, sess, frameEndInfo);
}

XrResult
oxr_xrRequestExitSession(XrSession session)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrRequestExitSession");

	return oxr_session_request_exit(&log, sess);
}

XrResult
oxr_xrLocateViews(XrSession session,
                  const XrViewLocateInfo *viewLocateInfo,
                  XrViewState *viewState,
                  uint32_t viewCapacityInput,
                  uint32_t *viewCountOutput,
                  XrView *views)
{
	struct oxr_session *sess;
	struct oxr_space *spc;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrLocateViews");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, viewLocateInfo,
	                                 XR_TYPE_VIEW_LOCATE_INFO);
	OXR_VERIFY_SPACE_NOT_NULL(&log, viewLocateInfo->space, spc);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, viewState, XR_TYPE_VIEW_STATE);

	if (viewCapacityInput == 0) {
		OXR_VERIFY_ARG_NOT_NULL(&log, viewCountOutput);
	} else {
		OXR_VERIFY_ARG_NOT_NULL(&log, views);
	}

	return oxr_session_views(&log, sess, viewLocateInfo, viewState,
	                         viewCapacityInput, viewCountOutput, views);
}


/*
 *
 * XR_KHR_visibility_mask
 *
 */

#ifdef XR_KHR_visibility_mask

XrResult
oxr_xrGetVisibilityMaskKHR(XrSession session,
                           XrViewConfigurationType viewConfigurationType,
                           uint32_t viewIndex,
                           XrVisibilityMaskTypeKHR visibilityMaskType,
                           XrVisibilityMaskKHR *visibilityMask)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrGetVisibilityMaskKHR");

	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

#endif


/*
 *
 * XR_EXT_performance_settings
 *
 */

#ifdef XR_EXT_performance_settings

XrResult
oxr_xrPerfSettingsSetPerformanceLevelEXT(XrSession session,
                                         XrPerfSettingsDomainEXT domain,
                                         XrPerfSettingsLevelEXT level)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrPerfSettingsSetPerformanceLevelEXT");

	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

#endif


/*
 *
 * XR_EXT_thermal_query
 *
 */

#ifdef XR_EXT_thermal_query

XrResult
oxr_xrThermalGetTemperatureTrendEXT(
    XrSession session,
    XrPerfSettingsDomainEXT domain,
    XrPerfSettingsNotificationLevelEXT *notificationLevel,
    float *tempHeadroom,
    float *tempSlope)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrThermalGetTemperatureTrendEXT");

	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

#endif
