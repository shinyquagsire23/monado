// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Action related API entrypoint functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_api
 */

#include <stdio.h>

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_handle.h"

#include "util/u_debug.h"

#include "oxr_api_funcs.h"
#include "oxr_api_verify.h"


/*
 *
 * Session - action functions.
 *
 */

XrResult
oxr_xrSyncActionData(XrSession session,
                     uint32_t countActionSets,
                     const XrActiveActionSet* actionSets)
{
	struct oxr_session* sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrSyncActionData");

	if (countActionSets == 0) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE,
		                 "(countActionSets == 0)");
	}

	for (uint32_t i = 0; i < countActionSets; i++) {
		struct oxr_action_set* act_set;
		OXR_VERIFY_ARG_TYPE_AND_NULL(&log, (&actionSets[i]),
		                             XR_TYPE_ACTIVE_ACTION_SET);
		OXR_VERIFY_ACTIONSET_NOT_NULL(&log, actionSets[i].actionSet,
		                              act_set);
		//! @todo verify path.
	}

	//! @todo Implement
	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_xrSetInteractionProfileSuggestedBindings(
    XrSession session,
    const XrInteractionProfileSuggestedBinding* suggestedBindings)
{
	struct oxr_session* sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(
	    &log, session, sess, "xrSetInteractionProfileSuggestedBindings");
	OXR_VERIFY_ARG_TYPE_AND_NULL(
	    &log, suggestedBindings,
	    XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING);

	//! @todo Implement
	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_xrGetCurrentInteractionProfile(XrSession session,
                                   XrPath topLevelUserPath,
                                   XrInteractionProfileInfo* interactionProfile)
{
	struct oxr_session* sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrGetCurrentInteractionProfile");
	OXR_VERIFY_ARG_TYPE_AND_NULL(&log, interactionProfile,
	                             XR_TYPE_INTERACTION_PROFILE_INFO);

	//! @todo Implement
	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_xrGetInputSourceLocalizedName(
    XrSession session,
    XrPath source,
    XrInputSourceLocalizedNameFlags whichComponents,
    uint32_t bufferCapacityInput,
    uint32_t* bufferCountOutput,
    char* buffer)
{
	struct oxr_session* sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrGetInputSourceLocalizedName");
	//! @todo verify path

	//! @todo Implement
	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}


/*
 *
 * Action set functions
 *
 */

static XrResult
oxr_action_set_destroy(struct oxr_logger* log, struct oxr_handle_base* hb)
{
	//! @todo Move to oxr_action.h
	struct oxr_action_set* act_set = (struct oxr_action_set*)hb;

	free(act_set);

	return XR_SUCCESS;
}

XrResult
oxr_xrCreateActionSet(XrSession session,
                      const XrActionSetCreateInfo* createInfo,
                      XrActionSet* actionSet)
{
	struct oxr_session* sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrCreateActionSet");
	OXR_VERIFY_ARG_TYPE_AND_NULL(&log, createInfo,
	                             XR_TYPE_ACTION_SET_CREATE_INFO);
	OXR_VERIFY_ARG_NOT_NULL(&log, actionSet);
	OXR_VERIFY_ARG_SINGLE_LEVEL_FIXED_LENGTH_PATH(
	    &log, createInfo->actionSetName);
	OXR_VERIFY_ARG_LOCALIZED_NAME(&log, createInfo->localizedActionSetName);

	//! @todo Move to oxr_action.h and implement more fully.
	struct oxr_action_set* act_set = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(&log, act_set, OXR_XR_DEBUG_ACTIONSET,
	                              oxr_action_set_destroy, &sess->handle);
	act_set->sess = sess;

	*actionSet = (XrActionSet)act_set;

	return XR_SUCCESS;
}

XrResult
oxr_xrDestroyActionSet(XrActionSet actionSet)
{
	struct oxr_action_set* act_set;
	struct oxr_logger log;
	OXR_VERIFY_ACTIONSET_AND_INIT_LOG(&log, actionSet, act_set,
	                                  "xrDestroyActionSet");

	return oxr_handle_destroy(&log, &act_set->handle);
}


/*
 *
 * Action functions
 *
 */

static XrResult
oxr_action_destroy(struct oxr_logger* log, struct oxr_handle_base* hb)
{
	//! @todo Move to oxr_action.h
	struct oxr_action* act = (struct oxr_action*)hb;

	free(act);

	return XR_SUCCESS;
}

XrResult
oxr_xrCreateAction(XrActionSet actionSet,
                   const XrActionCreateInfo* createInfo,
                   XrAction* action)
{
	struct oxr_action_set* act_set;
	struct oxr_logger log;
	OXR_VERIFY_ACTIONSET_AND_INIT_LOG(&log, actionSet, act_set,
	                                  "xrCreateAction");
	OXR_VERIFY_ARG_TYPE_AND_NULL(&log, createInfo,
	                             XR_TYPE_ACTION_CREATE_INFO);
	OXR_VERIFY_ARG_SINGLE_LEVEL_FIXED_LENGTH_PATH(&log,
	                                              createInfo->actionName);
	OXR_VERIFY_ARG_LOCALIZED_NAME(&log, createInfo->localizedActionName);
	OXR_VERIFY_ARG_NOT_NULL(&log, action);

	//! @todo Move to oxr_action.h and implement more fully.
	struct oxr_action* act = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(&log, act, OXR_XR_DEBUG_ACTION,
	                              oxr_action_destroy, &act_set->handle);
	*action = (XrAction)act;

	return XR_SUCCESS;
}

XrResult
oxr_xrDestroyAction(XrAction action)
{
	struct oxr_action* act;
	struct oxr_logger log;
	OXR_VERIFY_ACTION_AND_INIT_LOG(&log, action, act, "xrDestroyAction");

	return oxr_handle_destroy(&log, &act->handle);
}

XrResult
oxr_xrGetActionStateBoolean(XrAction action,
                            uint32_t countSubactionPaths,
                            const XrPath* subactionPaths,
                            XrActionStateBoolean* data)
{
	struct oxr_action* act;
	struct oxr_logger log;
	OXR_VERIFY_ACTION_AND_INIT_LOG(&log, action, act,
	                               "xrGetActionStateBoolean");
	OXR_VERIFY_ARG_TYPE_AND_NULL(&log, data, XR_TYPE_ACTION_STATE_BOOLEAN);
	OXR_VERIFY_SUBACTION_PATHS(&log, countSubactionPaths, subactionPaths);
	//! @todo verify paths

	//! @todo Implement
	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_xrGetActionStateVector1f(XrAction action,
                             uint32_t countSubactionPaths,
                             const XrPath* subactionPaths,
                             XrActionStateVector1f* data)
{
	struct oxr_action* act;
	struct oxr_logger log;
	OXR_VERIFY_ACTION_AND_INIT_LOG(&log, action, act,
	                               "xrGetActionStateVector1f");
	OXR_VERIFY_ARG_TYPE_AND_NULL(&log, data, XR_TYPE_ACTION_STATE_VECTOR1F);
	OXR_VERIFY_SUBACTION_PATHS(&log, countSubactionPaths, subactionPaths);
	//! @todo verify paths

	//! @todo Implement
	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_xrGetActionStateVector2f(XrAction action,
                             uint32_t countSubactionPaths,
                             const XrPath* subactionPaths,
                             XrActionStateVector2f* data)
{
	struct oxr_action* act;
	struct oxr_logger log;
	OXR_VERIFY_ACTION_AND_INIT_LOG(&log, action, act,
	                               "xrGetActionStateVector2f");
	OXR_VERIFY_ARG_TYPE_AND_NULL(&log, data, XR_TYPE_ACTION_STATE_VECTOR2F);
	OXR_VERIFY_SUBACTION_PATHS(&log, countSubactionPaths, subactionPaths);
	//! @todo verify paths

	//! @todo Implement
	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_xrGetActionStatePose(XrAction action,
                         XrPath subactionPath,
                         XrActionStatePose* data)
{
	struct oxr_action* act;
	struct oxr_logger log;
	OXR_VERIFY_ACTION_AND_INIT_LOG(&log, action, act,
	                               "xrGetActionStatePose");
	OXR_VERIFY_ARG_TYPE_AND_NULL(&log, data, XR_TYPE_ACTION_STATE_POSE);
	//! @todo verify path

	//! @todo Implement
	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_xrGetBoundSourcesForAction(XrAction action,
                               uint32_t sourceCapacityInput,
                               uint32_t* sourceCountOutput,
                               XrPath* sources)
{
	struct oxr_action* act;
	struct oxr_logger log;
	OXR_VERIFY_ACTION_AND_INIT_LOG(&log, action, act,
	                               "xrGetBoundSourcesForAction");

	//! @todo Implement
	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}


/*
 *
 * Haptic feedback functions.
 *
 */

XrResult
oxr_xrApplyHapticFeedback(XrAction hapticAction,
                          uint32_t countSubactionPaths,
                          const XrPath* subactionPaths,
                          const XrHapticBaseHeader* hapticEvent)
{
	struct oxr_action* hapticAct;
	struct oxr_logger log;
	OXR_VERIFY_ACTION_AND_INIT_LOG(&log, hapticAction, hapticAct,
	                               "xrApplyHapticFeedback");
	OXR_VERIFY_SUBACTION_PATHS(&log, countSubactionPaths, subactionPaths);
	//! @todo verify paths

	//! @todo Implement
	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_xrStopHapticFeedback(XrAction hapticAction,
                         uint32_t countSubactionPaths,
                         const XrPath* subactionPaths)
{
	struct oxr_action* hapticAct;
	struct oxr_logger log;
	OXR_VERIFY_ACTION_AND_INIT_LOG(&log, hapticAction, hapticAct,
	                               "xrStopHapticFeedback");
	OXR_VERIFY_SUBACTION_PATHS(&log, countSubactionPaths, subactionPaths);
	//! @todo verify paths

	//! @todo Implement
	return oxr_error(&log, XR_ERROR_HANDLE_INVALID, " not implemented");
}
