// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Action related API entrypoint functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_api
 */

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_handle.h"

#include "util/u_debug.h"

#include "oxr_api_funcs.h"
#include "oxr_api_verify.h"
#include "oxr_subaction.h"

#include <stdio.h>
#include <inttypes.h>

#include "bindings/b_generated_bindings.h"

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
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, syncInfo, XR_TYPE_ACTIONS_SYNC_INFO);

	if (syncInfo->countActiveActionSets == 0) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE, "(syncInfo->countActiveActionSets == 0)");
	}

	for (uint32_t i = 0; i < syncInfo->countActiveActionSets; i++) {
		struct oxr_action_set *act_set = NULL;
		OXR_VERIFY_ACTIONSET_NOT_NULL(&log, syncInfo->activeActionSets[i].actionSet, act_set);

		XrResult res = oxr_verify_subaction_path_sync(&log, sess->sys->inst,
		                                              syncInfo->activeActionSets[i].subactionPath, i);
		if (res != XR_SUCCESS) {
			return res;
		}
	}

	return oxr_action_sync_data(&log, sess, syncInfo->countActiveActionSets, syncInfo->activeActionSets);
}

XrResult
oxr_xrAttachSessionActionSets(XrSession session, const XrSessionActionSetsAttachInfo *bindInfo)
{
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrAttachSessionActionSets");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, bindInfo, XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO);

	if (sess->act_set_attachments != NULL) {
		return oxr_error(&log, XR_ERROR_ACTIONSETS_ALREADY_ATTACHED,
		                 "(session) has already had action sets "
		                 "attached, can only attach action sets once.");
	}

	if (bindInfo->countActionSets == 0) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE,
		                 "(bindInfo->countActionSets == 0) must attach "
		                 "at least one action set.");
	}

	for (uint32_t i = 0; i < bindInfo->countActionSets; i++) {
		struct oxr_action_set *act_set = NULL;
		OXR_VERIFY_ACTIONSET_NOT_NULL(&log, bindInfo->actionSets[i], act_set);
	}

	return oxr_session_attach_action_sets(&log, sess, bindInfo);
}

XrResult
oxr_xrSuggestInteractionProfileBindings(XrInstance instance,
                                        const XrInteractionProfileSuggestedBinding *suggestedBindings)
{
	struct oxr_instance *inst;
	struct oxr_logger log;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrSuggestInteractionProfileBindings");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, suggestedBindings, XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING);

	if (suggestedBindings->countSuggestedBindings == 0) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE,
		                 "(suggestedBindings->countSuggestedBindings "
		                 "== 0) can not suggest 0 bindings");
	}

	XrPath ip = suggestedBindings->interactionProfile;
	const char *str = NULL;
	size_t length;

	XrResult ret = oxr_path_get_string(&log, inst, ip, &str, &length);
	if (ret != XR_SUCCESS) {
		oxr_error(&log, ret, "(suggestedBindings->countSuggestedBindings == 0x%08" PRIx64 ") invalid path", ip);
	}

	// Used in the loop that verifies the suggested bindings paths.
	bool (*func)(const char *, size_t) = NULL;

	if (ip == inst->path_cache.khr_simple_controller) {
		func = oxr_verify_khr_simple_controller_subpath;
	} else if (ip == inst->path_cache.google_daydream_controller) {
		func = oxr_verify_google_daydream_controller_subpath;
	} else if (ip == inst->path_cache.htc_vive_controller) {
		func = oxr_verify_htc_vive_controller_subpath;
	} else if (ip == inst->path_cache.htc_vive_pro) {
		func = oxr_verify_htc_vive_pro_subpath;
	} else if (ip == inst->path_cache.microsoft_motion_controller) {
		func = oxr_verify_microsoft_motion_controller_subpath;
	} else if (ip == inst->path_cache.microsoft_xbox_controller) {
		func = oxr_verify_microsoft_xbox_controller_subpath;
	} else if (ip == inst->path_cache.oculus_go_controller) {
		func = oxr_verify_oculus_go_controller_subpath;
	} else if (ip == inst->path_cache.oculus_touch_controller) {
		func = oxr_verify_oculus_touch_controller_subpath;
	} else if (ip == inst->path_cache.valve_index_controller) {
		func = oxr_verify_valve_index_controller_subpath;
	} else if (ip == inst->path_cache.mndx_ball_on_a_stick_controller) {
		func = oxr_verify_mndx_ball_on_a_stick_controller_subpath;
	} else {
		return oxr_error(&log, XR_ERROR_PATH_UNSUPPORTED,
		                 "(suggestedBindings->interactionProfile == \"%s\") is not "
		                 "a supported interaction profile",
		                 str);
	}


	for (size_t i = 0; i < suggestedBindings->countSuggestedBindings; i++) {
		const XrActionSuggestedBinding *s = &suggestedBindings->suggestedBindings[i];

		struct oxr_action *act;
		OXR_VERIFY_ACTION_NOT_NULL(&log, s->action, act);

		if (act->act_set->data->ever_attached) {
			return oxr_error(&log, XR_ERROR_ACTIONSETS_ALREADY_ATTACHED,
			                 "(suggestedBindings->suggestedBindings[%zu]->"
			                 "action) action '%s/%s' has already been attached",
			                 i, act->act_set->data->name, act->data->name);
		}

		ret = oxr_path_get_string(&log, inst, s->binding, &str, &length);
		if (ret != XR_SUCCESS) {
			return oxr_error(&log, XR_ERROR_PATH_INVALID,
			                 "(suggestedBindings->suggestedBindings[%zu]->"
			                 "binding == %" PRIu64 ") is not a valid path",
			                 i, s->binding);
		}

		if (!func(str, length)) {
			return oxr_error(&log, XR_ERROR_PATH_UNSUPPORTED,
			                 "(suggestedBindings->suggestedBindings[%zu]->"
			                 "binding == \"%s\") is not a valid path",
			                 i, str);
		}
	}

	return oxr_action_suggest_interaction_profile_bindings(&log, inst, suggestedBindings);
}

XrResult
oxr_xrGetCurrentInteractionProfile(XrSession session,
                                   XrPath topLevelUserPath,
                                   XrInteractionProfileState *interactionProfile)
{
	struct oxr_instance *inst = NULL;
	struct oxr_session *sess = NULL;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrGetCurrentInteractionProfile");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, interactionProfile, XR_TYPE_INTERACTION_PROFILE_STATE);

	// Short hand.
	inst = sess->sys->inst;

	if (topLevelUserPath == XR_NULL_PATH) {
		return oxr_error(&log, XR_ERROR_PATH_INVALID,
		                 "(topLevelUserPath == XR_NULL_PATH) The null "
		                 "path is not a valid argument");
	}

	if (!oxr_path_is_valid(&log, inst, topLevelUserPath)) {
		return oxr_error(&log, XR_ERROR_PATH_INVALID, "(topLevelUserPath == %zu) Is not a valid path",
		                 topLevelUserPath);
	}

	bool fail = true;
#define COMPUTE_FAIL(X)                                                                                                \
	if (topLevelUserPath == inst->path_cache.X) {                                                                  \
		fail = false;                                                                                          \
	}

	OXR_FOR_EACH_SUBACTION_PATH(COMPUTE_FAIL)
#undef COMPUTE_FAIL
	if (fail) {
		const char *str = NULL;
		size_t len = 0;
		oxr_path_get_string(&log, inst, topLevelUserPath, &str, &len);

		return oxr_error(&log, XR_ERROR_PATH_UNSUPPORTED,
		                 "(topLevelUserPath == %s) Is not a valid top "
		                 "level user path",
		                 str);
	}

	/* XXX: How do we return XR_SESSION_LOSS_PENDING here? */
	return oxr_action_get_current_interaction_profile(&log, sess, topLevelUserPath, interactionProfile);
}

XrResult
oxr_xrGetInputSourceLocalizedName(XrSession session,
                                  const XrInputSourceLocalizedNameGetInfo *getInfo,
                                  uint32_t bufferCapacityInput,
                                  uint32_t *bufferCountOutput,
                                  char *buffer)
{
	struct oxr_instance *inst = NULL;
	struct oxr_session *sess;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrGetInputSourceLocalizedName");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, getInfo, XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO);

	// Short hand.
	inst = sess->sys->inst;

	if (sess->act_set_attachments == NULL) {
		return oxr_error(&log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		                 "ActionSet(s) have not been attached to this session");
	}

	if (getInfo->sourcePath == XR_NULL_PATH) {
		return oxr_error(&log, XR_ERROR_PATH_INVALID,
		                 "(getInfo->sourcePath == XR_NULL_PATH) The "
		                 "null path is not a valid argument");
	}

	if (!oxr_path_is_valid(&log, inst, getInfo->sourcePath)) {
		return oxr_error(&log, XR_ERROR_PATH_INVALID, "(getInfo->sourcePath == %zu) Is not a valid path",
		                 getInfo->sourcePath);
	}

	const XrInputSourceLocalizedNameFlags all = XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT |
	                                            XR_INPUT_SOURCE_LOCALIZED_NAME_INTERACTION_PROFILE_BIT |
	                                            XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT;

	if ((getInfo->whichComponents & ~all) != 0) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE,
		                 "(getInfo->whichComponents == %08zx) contains invalid bits", getInfo->whichComponents);
	}

	if (getInfo->whichComponents == 0) {
		return oxr_error(&log, XR_ERROR_VALIDATION_FAILURE, "(getInfo->whichComponents == 0) can not be zero");
	}

	return oxr_action_get_input_source_localized_name(&log, sess, getInfo, bufferCapacityInput, bufferCountOutput,
	                                                  buffer);
}


/*
 *
 * Action set functions
 *
 */

XrResult
oxr_xrCreateActionSet(XrInstance instance, const XrActionSetCreateInfo *createInfo, XrActionSet *actionSet)
{
	struct oxr_action_set *act_set = NULL;
	struct oxr_instance *inst = NULL;
	struct u_hashset_item *d = NULL;
	struct oxr_logger log;
	int h_ret;
	XrResult ret;
	OXR_VERIFY_INSTANCE_AND_INIT_LOG(&log, instance, inst, "xrCreateActionSet");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, createInfo, XR_TYPE_ACTION_SET_CREATE_INFO);
	OXR_VERIFY_ARG_NOT_NULL(&log, actionSet);
	OXR_VERIFY_ARG_SINGLE_LEVEL_FIXED_LENGTH_PATH(&log, createInfo->actionSetName);
	OXR_VERIFY_ARG_LOCALIZED_NAME(&log, createInfo->localizedActionSetName);


	/*
	 * Dup checks.
	 */

	h_ret = u_hashset_find_c_str(inst->action_sets.name_store, createInfo->actionSetName, &d);
	if (h_ret >= 0) {
		return oxr_error(&log, XR_ERROR_NAME_DUPLICATED, "(createInfo->actionSetName == '%s') is duplicated",
		                 createInfo->actionSetName);
	}

	h_ret = u_hashset_find_c_str(inst->action_sets.loc_store, createInfo->localizedActionSetName, &d);
	if (h_ret >= 0) {
		return oxr_error(&log, XR_ERROR_LOCALIZED_NAME_DUPLICATED,
		                 "(createInfo->localizedActionSetName == '%s') "
		                 "is duplicated",
		                 createInfo->localizedActionSetName);
	}


	/*
	 * All ok.
	 */

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
	OXR_VERIFY_ACTIONSET_AND_INIT_LOG(&log, actionSet, act_set, "xrDestroyActionSet");

	return oxr_handle_destroy(&log, &act_set->handle);
}


/*
 *
 * Action functions
 *
 */

XrResult
oxr_xrCreateAction(XrActionSet actionSet, const XrActionCreateInfo *createInfo, XrAction *action)
{
	struct oxr_action_set *act_set;
	struct u_hashset_item *d = NULL;
	struct oxr_action *act = NULL;
	struct oxr_logger log;
	XrResult ret;
	int h_ret;

	OXR_VERIFY_ACTIONSET_AND_INIT_LOG(&log, actionSet, act_set, "xrCreateAction");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, createInfo, XR_TYPE_ACTION_CREATE_INFO);
	OXR_VERIFY_ARG_SINGLE_LEVEL_FIXED_LENGTH_PATH(&log, createInfo->actionName);
	OXR_VERIFY_ARG_LOCALIZED_NAME(&log, createInfo->localizedActionName);
	OXR_VERIFY_ARG_NOT_NULL(&log, action);

	if (act_set->data->ever_attached) {
		return oxr_error(&log, XR_ERROR_ACTIONSETS_ALREADY_ATTACHED,
		                 "(actionSet) has been attached and is now immutable");
	}

	struct oxr_instance *inst = act_set->inst;

	ret = oxr_verify_subaction_paths_create(&log, inst, createInfo->countSubactionPaths, createInfo->subactionPaths,
	                                        "createInfo->subactionPaths");
	if (ret != XR_SUCCESS) {
		return ret;
	}


	/*
	 * Dup checks.
	 */

	h_ret = u_hashset_find_c_str(act_set->data->actions.name_store, createInfo->actionName, &d);
	if (h_ret >= 0) {
		return oxr_error(&log, XR_ERROR_NAME_DUPLICATED, "(createInfo->actionName == '%s') is duplicated",
		                 createInfo->actionName);
	}

	h_ret = u_hashset_find_c_str(act_set->data->actions.loc_store, createInfo->localizedActionName, &d);
	if (h_ret >= 0) {
		return oxr_error(&log, XR_ERROR_LOCALIZED_NAME_DUPLICATED,
		                 "(createInfo->localizedActionName == '%s') "
		                 "is duplicated",
		                 createInfo->localizedActionName);
	}


	/*
	 * All ok.
	 */

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
oxr_xrGetActionStateBoolean(XrSession session, const XrActionStateGetInfo *getInfo, XrActionStateBoolean *data)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_subaction_paths subaction_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrGetActionStateBoolean");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, data, XR_TYPE_ACTION_STATE_BOOLEAN);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, getInfo, XR_TYPE_ACTION_STATE_GET_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, getInfo->action, act);

	if (act->data->action_type != XR_ACTION_TYPE_BOOLEAN_INPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH, "Not created with boolean type");
	}

	ret = oxr_verify_subaction_path_get(&log, act->act_set->inst, getInfo->subactionPath,
	                                    &act->data->subaction_paths, &subaction_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	return oxr_action_get_boolean(&log, sess, act->act_key, subaction_paths, data);
}

XrResult
oxr_xrGetActionStateFloat(XrSession session, const XrActionStateGetInfo *getInfo, XrActionStateFloat *data)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_subaction_paths subaction_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrGetActionStateFloat");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, data, XR_TYPE_ACTION_STATE_FLOAT);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, getInfo, XR_TYPE_ACTION_STATE_GET_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, getInfo->action, act);

	if (act->data->action_type != XR_ACTION_TYPE_FLOAT_INPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH, "Not created with float type");
	}

	ret = oxr_verify_subaction_path_get(&log, act->act_set->inst, getInfo->subactionPath,
	                                    &act->data->subaction_paths, &subaction_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	return oxr_action_get_vector1f(&log, sess, act->act_key, subaction_paths, data);
}

XrResult
oxr_xrGetActionStateVector2f(XrSession session, const XrActionStateGetInfo *getInfo, XrActionStateVector2f *data)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_subaction_paths subaction_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrGetActionStateVector2f");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, data, XR_TYPE_ACTION_STATE_VECTOR2F);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, getInfo, XR_TYPE_ACTION_STATE_GET_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, getInfo->action, act);

	if (act->data->action_type != XR_ACTION_TYPE_VECTOR2F_INPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH, "Not created with float[2] type");
	}

	ret = oxr_verify_subaction_path_get(&log, act->act_set->inst, getInfo->subactionPath,
	                                    &act->data->subaction_paths, &subaction_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	return oxr_action_get_vector2f(&log, sess, act->act_key, subaction_paths, data);
}

XrResult
oxr_xrGetActionStatePose(XrSession session, const XrActionStateGetInfo *getInfo, XrActionStatePose *data)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_subaction_paths subaction_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrGetActionStatePose");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, data, XR_TYPE_ACTION_STATE_POSE);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, getInfo, XR_TYPE_ACTION_STATE_GET_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, getInfo->action, act);

	if (act->data->action_type != XR_ACTION_TYPE_POSE_INPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH, "Not created with pose type");
	}

	ret = oxr_verify_subaction_path_get(&log, act->act_set->inst, getInfo->subactionPath,
	                                    &act->data->subaction_paths, &subaction_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	return oxr_action_get_pose(&log, sess, act->act_key, subaction_paths, data);
}

XrResult
oxr_xrEnumerateBoundSourcesForAction(XrSession session,
                                     const XrBoundSourcesForActionEnumerateInfo *enumerateInfo,
                                     uint32_t sourceCapacityInput,
                                     uint32_t *sourceCountOutput,
                                     XrPath *sources)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_logger log;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrEnumerateBoundSourcesForAction");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, enumerateInfo, XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, enumerateInfo->action, act);

	if (sess->act_set_attachments == NULL) {
		return oxr_error(&log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		                 "(session) xrAttachSessionActionSets has not "
		                 "been called on this session.");
	}

	return oxr_action_enumerate_bound_sources(&log, sess, act->act_key, sourceCapacityInput, sourceCountOutput,
	                                          sources);
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
	struct oxr_subaction_paths subaction_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrApplyHapticFeedback");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, hapticActionInfo, XR_TYPE_HAPTIC_ACTION_INFO);
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, hapticEvent, XR_TYPE_HAPTIC_VIBRATION);
	OXR_VERIFY_ACTION_NOT_NULL(&log, hapticActionInfo->action, act);

	ret = oxr_verify_subaction_path_get(&log, act->act_set->inst, hapticActionInfo->subactionPath,
	                                    &act->data->subaction_paths, &subaction_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (act->data->action_type != XR_ACTION_TYPE_VIBRATION_OUTPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH, "Not created with output vibration type");
	}

	return oxr_action_apply_haptic_feedback(&log, sess, act->act_key, subaction_paths, hapticEvent);
}

XrResult
oxr_xrStopHapticFeedback(XrSession session, const XrHapticActionInfo *hapticActionInfo)
{
	struct oxr_session *sess = NULL;
	struct oxr_action *act = NULL;
	struct oxr_subaction_paths subaction_paths = {0};
	struct oxr_logger log;
	XrResult ret;
	OXR_VERIFY_SESSION_AND_INIT_LOG(&log, session, sess, "xrStopHapticFeedback");
	OXR_VERIFY_ARG_TYPE_AND_NOT_NULL(&log, hapticActionInfo, XR_TYPE_HAPTIC_ACTION_INFO);
	OXR_VERIFY_ACTION_NOT_NULL(&log, hapticActionInfo->action, act);

	ret = oxr_verify_subaction_path_get(&log, act->act_set->inst, hapticActionInfo->subactionPath,
	                                    &act->data->subaction_paths, &subaction_paths, "getInfo->subactionPath");
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (act->data->action_type != XR_ACTION_TYPE_VIBRATION_OUTPUT) {
		return oxr_error(&log, XR_ERROR_ACTION_TYPE_MISMATCH, "Not created with output vibration type");
	}

	return oxr_action_stop_haptic_feedback(&log, sess, act->act_key, subaction_paths);
}
