// Copyright 2019-2022, Collabora, Ltd.
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
#include "util/u_trace_marker.h"

#include "oxr_api_funcs.h"
#include "oxr_api_verify.h"
#include "oxr_chain.h"
#include "oxr_subaction.h"

#include <stdio.h>
#include <inttypes.h>

#include "bindings/b_generated_bindings.h"


typedef bool (*path_verify_fn_t)(const char *, size_t);


/*
 *
 * Dpad functions.
 *
 */

#ifdef XR_EXT_dpad_binding
XrResult
process_dpad(struct oxr_logger *log,
             struct oxr_instance *inst,
             struct oxr_dpad_state *state,
             const XrInteractionProfileDpadBindingEXT *dpad,
             path_verify_fn_t dpad_emulator_fn,
             const char *prefix,
             const char *ip_str)
{
	const char *str = NULL;
	size_t length = 0;
	XrResult ret;

	ret = oxr_path_get_string(log, inst, dpad->binding, &str, &length);
	if (ret != XR_SUCCESS) {
		return oxr_error(log, XR_ERROR_PATH_INVALID, "(%s->binding == %" PRIu64 ") is not a valid path", prefix,
		                 dpad->binding);
	}

	if (!dpad_emulator_fn(str, length)) {
		return oxr_error(log, XR_ERROR_PATH_UNSUPPORTED,
		                 "(%s->binding == \"%s\") is not a valid dpad binding path for profile \"%s\"", prefix,
		                 str, ip_str);
	}

	ret = oxr_verify_XrInteractionProfileDpadBindingEXT(log, dpad, prefix);
	if (ret != XR_SUCCESS) {
		return ret;
	}

	if (dpad->actionSet == XR_NULL_HANDLE) {
		return oxr_error(log, XR_ERROR_HANDLE_INVALID, "(%s->actionSet == XR_NULL_HANDLE)", prefix);
	}

	struct oxr_action_set *act_set = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_action_set *, dpad->actionSet);
	if (act_set->handle.debug != OXR_XR_DEBUG_ACTIONSET) {
		return oxr_error(log, XR_ERROR_HANDLE_INVALID, "(%s->actionSet == %p)", prefix, (void *)act_set);
	}

	struct oxr_dpad_entry *entry = oxr_dpad_state_get_or_add(state, act_set->act_set_key);
	if (entry->key == 0) {
		entry->key = act_set->act_set_key;
		assert(act_set->act_set_key != 0);
	}

	bool added = false;
	for (size_t i = 0; i < ARRAY_SIZE(entry->dpads); i++) {
		// Have we found a empty slot, add it.
		if (entry->dpads[i].binding == XR_NULL_PATH) {
			struct oxr_dpad_binding_modification dpad_binding = {
			    .binding = dpad->binding,
			    .settings = {
			        .forceThreshold = dpad->forceThreshold,
			        .forceThresholdReleased = dpad->forceThresholdReleased,
			        .centerRegion = dpad->centerRegion,
			        .wedgeAngle = dpad->wedgeAngle,
			        .isSticky = dpad->isSticky,
			    }};
			entry->dpads[i] = dpad_binding;

			entry->dpad_count++;
			added = true;
			break;
		}

		if (entry->dpads[i].binding == dpad->binding) {
			return oxr_error(
			    log, XR_ERROR_VALIDATION_FAILURE,
			    "(%s->[actionSet == \"%s\", binding == \"%s\"]) pair is already added to profile \"%s\"",
			    prefix, act_set->data->name, str, ip_str);
		}
	}

	if (!added) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Failed to add dpad binding!");
	}

	return XR_SUCCESS;
}
#endif


/*
 *
 * Session - action functions.
 *
 */

XrResult
oxr_xrSyncActions(XrSession session, const XrActionsSyncInfo *syncInfo)
{
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
	const char *ip_str = NULL;
	size_t ip_length = 0;

	XrResult ret = oxr_path_get_string(&log, inst, ip, &ip_str, &ip_length);
	if (ret != XR_SUCCESS) {
		oxr_error(&log, ret, "(suggestedBindings->countSuggestedBindings == 0x%08" PRIx64 ") invalid path", ip);
	}

	// Used in the loop that verifies the suggested bindings paths.
	path_verify_fn_t subpath_fn = NULL;
	path_verify_fn_t dpad_path_fn = NULL;
	path_verify_fn_t dpad_emulator_fn = NULL;
	bool has_dpad = inst->extensions.EXT_dpad_binding;

	if (ip == inst->path_cache.khr_simple_controller) {
		subpath_fn = oxr_verify_khr_simple_controller_subpath;
		dpad_path_fn = oxr_verify_khr_simple_controller_dpad_path;
		dpad_emulator_fn = oxr_verify_khr_simple_controller_dpad_emulator;
	} else if (ip == inst->path_cache.google_daydream_controller) {
		subpath_fn = oxr_verify_google_daydream_controller_subpath;
		dpad_path_fn = oxr_verify_google_daydream_controller_dpad_path;
		dpad_emulator_fn = oxr_verify_google_daydream_controller_dpad_emulator;
	} else if (ip == inst->path_cache.htc_vive_controller) {
		subpath_fn = oxr_verify_htc_vive_controller_subpath;
		dpad_path_fn = oxr_verify_htc_vive_controller_dpad_path;
		dpad_emulator_fn = oxr_verify_htc_vive_controller_dpad_emulator;
	} else if (ip == inst->path_cache.htc_vive_pro) {
		subpath_fn = oxr_verify_htc_vive_pro_subpath;
		dpad_path_fn = oxr_verify_htc_vive_pro_dpad_path;
		dpad_emulator_fn = oxr_verify_htc_vive_pro_dpad_emulator;
	} else if (ip == inst->path_cache.microsoft_motion_controller) {
		subpath_fn = oxr_verify_microsoft_motion_controller_subpath;
		dpad_path_fn = oxr_verify_microsoft_motion_controller_dpad_path;
		dpad_emulator_fn = oxr_verify_microsoft_motion_controller_dpad_emulator;
	} else if (ip == inst->path_cache.microsoft_xbox_controller) {
		subpath_fn = oxr_verify_microsoft_xbox_controller_subpath;
		dpad_path_fn = oxr_verify_microsoft_xbox_controller_dpad_path;
		dpad_emulator_fn = oxr_verify_microsoft_xbox_controller_dpad_emulator;
	} else if (ip == inst->path_cache.oculus_go_controller) {
		subpath_fn = oxr_verify_oculus_go_controller_subpath;
		dpad_path_fn = oxr_verify_oculus_go_controller_dpad_path;
		dpad_emulator_fn = oxr_verify_oculus_go_controller_dpad_emulator;
	} else if (ip == inst->path_cache.oculus_touch_controller) {
		subpath_fn = oxr_verify_oculus_touch_controller_subpath;
		dpad_path_fn = oxr_verify_oculus_touch_controller_dpad_path;
		dpad_emulator_fn = oxr_verify_oculus_touch_controller_dpad_emulator;
	} else if (ip == inst->path_cache.valve_index_controller) {
		subpath_fn = oxr_verify_valve_index_controller_subpath;
		dpad_path_fn = oxr_verify_valve_index_controller_dpad_path;
		dpad_emulator_fn = oxr_verify_valve_index_controller_dpad_emulator;
	} else if (ip == inst->path_cache.mndx_ball_on_a_stick_controller) {
		subpath_fn = oxr_verify_mndx_ball_on_a_stick_controller_subpath;
		dpad_path_fn = oxr_verify_mndx_ball_on_a_stick_controller_dpad_path;
		dpad_emulator_fn = oxr_verify_mndx_ball_on_a_stick_controller_dpad_emulator;
	} else if (ip == inst->path_cache.msft_hand_interaction) {
		subpath_fn = oxr_verify_microsoft_hand_interaction_subpath;
		dpad_path_fn = oxr_verify_microsoft_hand_interaction_dpad_path;
		dpad_emulator_fn = oxr_verify_microsoft_hand_interaction_dpad_emulator;
	} else {
		return oxr_error(&log, XR_ERROR_PATH_UNSUPPORTED,
		                 "(suggestedBindings->interactionProfile == \"%s\") is not "
		                 "a supported interaction profile",
		                 ip_str);
	}

	// Needed in various paths here.
	const char *str = NULL;
	size_t length;

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

		if (subpath_fn(str, length)) {
			continue;
		}

#ifdef XR_EXT_dpad_binding
		if (dpad_path_fn(str, length)) {
			if (!has_dpad) {
				return oxr_error(
				    &log, XR_ERROR_PATH_UNSUPPORTED,
				    "(suggestedBindings->suggestedBindings[%zu]->binding == \"%s\") is is a dpad path, "
				    "but XR_EXT_dpad_binding is not enabled, for profile \"%s\"",
				    i, str, ip_str);
			}
			continue;
		}
#endif

		return oxr_error(&log, XR_ERROR_PATH_UNSUPPORTED,
		                 "(suggestedBindings->suggestedBindings[%zu]->binding == \"%s\") is not a valid "
		                 "binding path for profile \"%s\"",
		                 i, str, ip_str);
	}


	/*
	 * Binding modifications.
	 */

	const XrBindingModificationsKHR *mods = OXR_GET_INPUT_FROM_CHAIN(
	    suggestedBindings->next, XR_TYPE_BINDING_MODIFICATIONS_KHR, XrBindingModificationsKHR);

	struct oxr_dpad_state dpad_state = {0};
#ifdef XR_EXT_dpad_binding
	if (has_dpad) {
		if (!oxr_dpad_state_init(&dpad_state)) {
			return oxr_error(&log, XR_ERROR_RUNTIME_FAILURE, " Failed to init dpad state!");
		}

		char temp[512] = {0};
		for (uint32_t i = 0; mods != NULL && i < mods->bindingModificationCount; i++) {
			const XrBindingModificationBaseHeaderKHR *mod = mods->bindingModifications[i];
			const XrInteractionProfileDpadBindingEXT *dpad = OXR_GET_INPUT_FROM_CHAIN(
			    mod, XR_TYPE_INTERACTION_PROFILE_DPAD_BINDING_EXT, XrInteractionProfileDpadBindingEXT);
			if (dpad == NULL) {
				continue;
			}

			snprintf(temp, sizeof(temp),
			         "suggestedBindings->next<XrBindingModificationsKHR>->bindingModifications[%u]->next<"
			         "XrInteractionProfileDpadBindingEXT>",
			         i);

			ret = process_dpad(&log, inst, &dpad_state, dpad, dpad_emulator_fn, temp, ip_str);
			if (ret != XR_SUCCESS) {
				// Teardown the state.
				oxr_dpad_state_deinit(&dpad_state);
				return ret;
			}
		}
	}
#endif


	/*
	 * Everything verified.
	 */

	return oxr_action_suggest_interaction_profile_bindings(&log, inst, suggestedBindings, &dpad_state);
}

XrResult
oxr_xrGetCurrentInteractionProfile(XrSession session,
                                   XrPath topLevelUserPath,
                                   XrInteractionProfileState *interactionProfile)
{
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

	struct oxr_action *act;
	struct oxr_logger log;
	OXR_VERIFY_ACTION_AND_INIT_LOG(&log, action, act, "xrDestroyAction");

	return oxr_handle_destroy(&log, &act->handle);
}

XrResult
oxr_xrGetActionStateBoolean(XrSession session, const XrActionStateGetInfo *getInfo, XrActionStateBoolean *data)
{
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
	OXR_TRACE_MARKER();

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
