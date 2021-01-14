// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Space, space, space, SPAAAAAAAAAAAAAAAAAAAAAAAAAACE!
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_api
 */

#include "xrt/xrt_compiler.h"

#include "math/m_api.h"
#include "util/u_debug.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"

#include "oxr_api_funcs.h"
#include "oxr_api_verify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


XrResult
oxr_xrCreateActionSpace(XrSession session, const XrActionSpaceCreateInfo *createInfo, XrSpace *space)
{
	struct oxr_session *sess;
	struct oxr_action *act;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrCreateActionSpace");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, createInfo, XR_TYPE_ACTION_SPACE_CREATE_INFO);
	OXR_VERIFY_POSE(&log, createInfo->poseInActionSpace);
	OXR_VERIFY_ACTION_NOT_NULL(&log, createInfo->action, act);

	struct oxr_space *spc;
	XrResult ret = oxr_space_action_create(&log, sess, act->act_key, createInfo, &spc);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*space = oxr_space_to_openxr(spc);

	return oxr_session_success_result(sess);
}

static const XrReferenceSpaceType session_spaces[] = {
    XR_REFERENCE_SPACE_TYPE_VIEW,
    XR_REFERENCE_SPACE_TYPE_LOCAL,
    XR_REFERENCE_SPACE_TYPE_STAGE,
};

XrResult
oxr_xrEnumerateReferenceSpaces(XrSession session,
                               uint32_t spaceCapacityInput,
                               uint32_t *spaceCountOutput,
                               XrReferenceSpaceType *spaces)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrEnumerateReferenceSpaces");

	OXR_TWO_CALL_HELPER(&log, spaceCapacityInput, spaceCountOutput, spaces, ARRAY_SIZE(session_spaces),
	                    session_spaces, oxr_session_success_result(sess));
}

XrResult
oxr_xrGetReferenceSpaceBoundsRect(XrSession session, XrReferenceSpaceType referenceSpaceType, XrExtent2Df *bounds)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrGetReferenceSpaceBoundsRect");
	OXR_VERIFY_ARG_NOT_NULL(&log, bounds);


	switch (referenceSpaceType) {
	case XR_REFERENCE_SPACE_TYPE_VIEW:
	case XR_REFERENCE_SPACE_TYPE_LOCAL:
	case XR_REFERENCE_SPACE_TYPE_STAGE: break;
	default:
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE,
		                 "(referenceSpaceType == 0x%08x) is not a "
		                 "valid XrReferenceSpaceType",
		                 referenceSpaceType);
	}

	bounds->width = 0.0;
	bounds->height = 0.0;

	// Silently return that the bounds aren't available.
	return XR_SPACE_BOUNDS_UNAVAILABLE;
}

XrResult
oxr_xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo *createInfo, XrSpace *out_space)
{
	XrResult ret;
	struct oxr_session *sess;
	struct oxr_space *spc = NULL;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrCreateReferenceSpace");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, createInfo, XR_TYPE_REFERENCE_SPACE_CREATE_INFO);
	OXR_VERIFY_POSE(&log, createInfo->poseInReferenceSpace);

	ret = oxr_space_reference_create(&log, sess, createInfo, &spc);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*out_space = oxr_space_to_openxr(spc);

	return oxr_session_success_result(sess);
}

XrResult
oxr_xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation *location)
{
	struct oxr_space *spc;
	struct oxr_space *baseSpc;
	struct oxr_logger log;
	OXR_VERIFY_SPACE_AND_INIT_LOG(&log, space, spc, "xrLocateSpace");
	OXR_VERIFY_SPACE_NOT_NULL(&log, baseSpace, baseSpc);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, location, XR_TYPE_SPACE_LOCATION);

	OXR_VERIFY_ARG_TYPE_CAN_BE_NULL(&log, ((XrSpaceVelocity *)location->next), XR_TYPE_SPACE_VELOCITY);

	if (time <= (XrTime)0) {
		return oxr_error(&log, XR_ERROR_TIME_INVALID, "(time == %" PRIi64 ") is not a valid time.", time);
	}

	return oxr_space_locate(&log, spc, baseSpc, time, location);
}

XrResult
oxr_xrDestroySpace(XrSpace space)
{
	struct oxr_space *spc;
	struct oxr_logger log;
	OXR_VERIFY_SPACE_AND_INIT_LOG(&log, space, spc, "xrDestroySpace");

	return oxr_handle_destroy(&log, &spc->handle);
}
