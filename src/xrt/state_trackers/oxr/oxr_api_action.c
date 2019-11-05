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
oxr_xrSyncActions(XrSession session, const XrActionsSyncInfo *syncInfo)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrSyncActions");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, syncInfo,
	                                 XR_TYPE_ACTIONS_SYNC_INFO);

	if (syncInfo->countActiveActionSets == 0) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE,
		                 "(syncInfo->countActiveActionSets == 0)");
	}

	for (uint32_t i = 0; i < syncInfo->countActiveActionSets; i++) {
		struct oxr_action_set *act_set = NULL;
		OXR_VERIFY_ACTIONSET_NOT_NULL(
		    &log, syncInfo->activeActionSets[i].actionSet, act_set);

		oxr_verify_subaction_path_sync(
		    &log, sess->sys->inst,
		    syncInfo->activeActionSets[i].subactionPath, i);
	}

	return oxr_action_sync_data(&log, sess, syncInfo->countActiveActionSets,
	                            syncInfo->activeActionSets);
}

XrResult
oxr_xrAttachSessionActionSets(XrSession session,
                              const XrSessionActionSetsAttachInfo *bindInfo)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrAttachSessionActionSets");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(
	    &log, bindInfo, XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO);

	for (uint32_t i = 0; i < bindInfo->countActionSets; i++) {
		struct oxr_action_set *act_set = NULL;
		OXR_VERIFY_ACTIONSET_NOT_NULL(&log, bindInfo->actionSets[i],
		                              act_set);
	}

	return oxr_session_attach_action_sets(&log, sess, bindInfo);
}

XrResult
oxr_xrSuggestInteractionProfileBindings(
    XrInstance instance,
    const XrInteractionProfileSuggestedBinding *suggestedBindings)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst,
	                                 "xrSuggestInteractionProfileBindings");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(
	    &log, suggestedBindings,
	    XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING);

	for (size_t i = 0; i < suggestedBindings->countSuggestedBindings; i++) {
		const XrActionSuggestedBinding *s =
		    &suggestedBindings->suggestedBindings[i];

		struct oxr_action *dummy;
		OXR_VERIFY_ACTION_NOT_NULL(&log, s->action, dummy);

		//! @todo verify path (s->binding).
	}

	return oxr_action_suggest_interaction_profile_bindings(
	    &log, inst, suggestedBindings);
}

XrResult
oxr_xrGetCurrentInteractionProfile(
    XrSession session,
    XrPath topLevelUserPath,
    XrInteractionProfileState *interactionProfile)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrGetCurrentInteractionProfile");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, interactionProfile,
	                                 XR_TYPE_INTERACTION_PROFILE_STATE);

	/* XXX: How do we return XR_SESSION_LOSS_PENDING here? */
	return oxr_action_get_current_interaction_profile(
	    &log, sess, topLevelUserPath, interactionProfile);
}

XrResult
oxr_xrGetInputSourceLocalizedName(
    XrSession session,
    const XrInputSourceLocalizedNameGetInfo *getInfo,
    uint32_t bufferCapacityInput,
    uint32_t *bufferCountOutput,
    char *buffer)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrGetInputSourceLocalizedName");
	//! @todo verify getInfo

	return oxr_action_get_input_source_localized_name(
	    &log, sess, getInfo, bufferCapacityInput, bufferCountOutput,
	    buffer);
}


/*
 *
 * Action set functions
 *
 */

XrResult
oxr_xrCreateActionSet(XrInstance instance,
                      const XrActionSetCreateInfo *createInfo,
                      XrActionSet *actionSet)
{
	struct oxr_action_set *act_set = NULL;
	struct oxr_instance *inst = NULL;
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst,
	                                 "xrCreateActionSet");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, createInfo,
	                                 XR_TYPE_ACTION_SET_CREATE_INFO);
	OXR_VERIFY_ARG_NOT_NULL(&log, actionSet);
	OXR_VERIFY_ARG_SINGLE_LEVEL_FIXED_LENGTH_PATH(
	    &log, createInfo->actionSetName);
	OXR_VERIFY_ARG_LOCALIZED_NAME(&log, createInfo->localizedActionSetName);

	ret = oxr_action_set_create(&log, inst, createInfo, &act_set);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*actionSet = oxr_action_set_to_openxr(act_set);

	return XR_SUCCESS;
}

XrResult
oxr_xrDestroyActionSet(XrActionSet actionSet)
{
	struct oxr_action_set *act_set;
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

XrResult
oxr_xrCreateAction(XrActionSet actionSet,
                   const XrActionCreateInfo *createInfo,
                   XrAction *action)
{
	struct oxr_action_set *act_set;
	struct oxr_action *act = NULL;
	struct oxr_logger log;
	XrResult ret;

	OXR_VERIFY_ACTIONSET_AND_INIT_LOG(&log, actionSet, act_set,
	                                  "xrCreateAction");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, createInfo,
	                                 XR_TYPE_ACTION_CREATE_INFO);
	OXR_VERIFY_ARG_SINGLE_LEVEL_FIXED_LENGTH_PATH(&log,
	                                              createInfo->actionName);
	OXR_VERIFY_ARG_LOCALIZED_NAME(&log, createInfo->localizedActionName);
	OXR_VERIFY_ARG_NOT_NULL(&log, action);

	struct oxr_instance *inst = act_set->inst;

	ret = oxr_verify_subaction_paths_create(
	    &log, inst, createInfo->countSubactionPaths,
	    createInfo->subactionPaths, "createInfo->subactionPaths");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	ret = oxr_action_create(&log, act_set, createInfo, &act);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	*action = oxr_action_to_openxr(act);

	return XR_SUCCESS;
}

XrResult
oxr_xrDestroyAction(XrAction action)
{
	struct oxr_action *act;
	struct oxr_logger log;
	OXR_VERIFY_ACTION_AND_INIT_LOG(&log, action, act, "xrDestroyAction");

	return oxr_handle_destroy(&log, &act->handle);
}

XrResult
oxr_xrGetActionStateBoolean(XrSession session,
                            const XrActionStateGetInfo *getInfo,
                            XrActionStateBoolean *data)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_sub_paths sub_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrGetActionStateBoolean");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, data,
	                                 XR_TYPE_ACTION_STATE_BOOLEAN);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, getInfo,
	                                 XR_TYPE_ACTION_STATE_GET_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, getInfo->action, act);

	if (act->action_type != XR_ACTION_TYPE_BOOLEAN_INPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH,
		                 " not created with boolean type");
	}

	ret = oxr_verify_subaction_path_get(
	    &log, act->act_set->inst, getInfo->subactionPath, &act->sub_paths,
	    &sub_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	return oxr_action_get_boolean(&log, sess, act->key, sub_paths, data);
}

XrResult
oxr_xrGetActionStateFloat(XrSession session,
                          const XrActionStateGetInfo *getInfo,
                          XrActionStateFloat *data)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_sub_paths sub_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrGetActionStateFloat");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, data,
	                                 XR_TYPE_ACTION_STATE_FLOAT);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, getInfo,
	                                 XR_TYPE_ACTION_STATE_GET_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, getInfo->action, act);

	if (act->action_type != XR_ACTION_TYPE_FLOAT_INPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH,
		                 " not created with float type");
	}

	ret = oxr_verify_subaction_path_get(
	    &log, act->act_set->inst, getInfo->subactionPath, &act->sub_paths,
	    &sub_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	return oxr_action_get_vector1f(&log, sess, act->key, sub_paths, data);
}

XrResult
oxr_xrGetActionStateVector2f(XrSession session,
                             const XrActionStateGetInfo *getInfo,
                             XrActionStateVector2f *data)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_sub_paths sub_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrGetActionStateVector2f");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, data,
	                                 XR_TYPE_ACTION_STATE_VECTOR2F);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, getInfo,
	                                 XR_TYPE_ACTION_STATE_GET_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, getInfo->action, act);

	if (act->action_type != XR_ACTION_TYPE_VECTOR2F_INPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH,
		                 " not created with float[2] type");
	}

	ret = oxr_verify_subaction_path_get(
	    &log, act->act_set->inst, getInfo->subactionPath, &act->sub_paths,
	    &sub_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	return oxr_action_get_vector2f(&log, sess, act->key, sub_paths, data);
}

XrResult
oxr_xrGetActionStatePose(XrSession session,
                         const XrActionStateGetInfo *getInfo,
                         XrActionStatePose *data)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_sub_paths sub_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrGetActionStatePose");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, data, XR_TYPE_ACTION_STATE_POSE);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, getInfo,
	                                 XR_TYPE_ACTION_STATE_GET_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, getInfo->action, act);

	if (act->action_type != XR_ACTION_TYPE_POSE_INPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH,
		                 " not created with pose type");
	}

	ret = oxr_verify_subaction_path_get(
	    &log, act->act_set->inst, getInfo->subactionPath, &act->sub_paths,
	    &sub_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	return oxr_action_get_pose(&log, sess, act->key, sub_paths, data);
}

XrResult
oxr_xrEnumerateBoundSourcesForAction(
    XrSession session,
    const XrBoundSourcesForActionEnumerateInfo *enumerateInfo,
    uint32_t sourceCapacityInput,
    uint32_t *sourceCountOutput,
    XrPath *sources)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrEnumerateBoundSourcesForAction");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(
	    &log, enumerateInfo,
	    XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, enumerateInfo->action, act);

	return oxr_action_enumerate_bound_sources(&log, sess, act->key,
	                                          sourceCapacityInput,
	                                          sourceCountOutput, sources);
}


/*
 *
 * Haptic feedback functions.
 *
 */

XrResult
oxr_xrApplyHapticFeedback(XrSession session,
                          const XrHapticActionInfo *hapticActionInfo,
                          const XrHapticBaseHeader *hapticEvent)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_sub_paths sub_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrApplyHapticFeedback");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, hapticActionInfo,
	                                 XR_TYPE_HAPTIC_ACTION_INFO);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, hapticEvent,
	                                 XR_TYPE_HAPTIC_VIBRATION);
	OXR_VERIFY_ACTION_NOT_NULL(&log, hapticActionInfo->action, act);

	ret = oxr_verify_subaction_path_get(
	    &log, act->act_set->inst, hapticActionInfo->subactionPath,
	    &act->sub_paths, &sub_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (act->action_type != XR_ACTION_TYPE_VIBRATION_OUTPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH,
		                 " not created with output vibration type");
	}

	return oxr_action_apply_haptic_feedback(&log, sess, act->key, sub_paths,
	                                        hapticEvent);
}

XrResult
oxr_xrStopHapticFeedback(XrSession session,
                         const XrHapticActionInfo *hapticActionInfo)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_sub_paths sub_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess,
	                                "xrStopHapticFeedback");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, hapticActionInfo,
	                                 XR_TYPE_HAPTIC_ACTION_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, hapticActionInfo->action, act);

	ret = oxr_verify_subaction_path_get(
	    &log, act->act_set->inst, hapticActionInfo->subactionPath,
	    &act->sub_paths, &sub_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (act->action_type != XR_ACTION_TYPE_VIBRATION_OUTPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH,
		                 " not created with output vibration type");
	}

	return oxr_action_stop_haptic_feedback(&log, sess, act->key, sub_paths);
}
