// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds input related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "util/u_debug.h"
#include "util/u_time.h"
#include "util/u_misc.h"

#include "xrt/xrt_compiler.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_handle.h"


/*
 *
 * Struct and defines.
 *
 */

enum sub_action_path
{
	// clang-format off
	OXR_SUB_ACTION_PATH_USER    = 1 << 0,
	OXR_SUB_ACTION_PATH_HEAD    = 1 << 1,
	OXR_SUB_ACTION_PATH_LEFT    = 1 << 2,
	OXR_SUB_ACTION_PATH_RIGHT   = 1 << 3,
	OXR_SUB_ACTION_PATH_GAMEPAD = 1 << 4,
	// clang-format on
};


/*
 *
 * Pre declare functions.
 *
 */

static void
oxr_session_get_source_set(struct oxr_session* sess,
                           XrActionSet actionSet,
                           struct oxr_source_set** src_set,
                           struct oxr_action_set** act_set);

static void
oxr_session_get_source(struct oxr_session* sess,
                       uint32_t act_key,
                       struct oxr_source** out_src);

static void
oxr_source_cache_update(struct oxr_logger* log,
                        struct oxr_session* sess,
                        struct oxr_action* act,
                        struct oxr_source_cache* cache,
                        int64_t time,
                        bool select);

static void
oxr_source_update(struct oxr_logger* log,
                  struct oxr_session* sess,
                  struct oxr_action* act,
                  int64_t time,
                  struct oxr_sub_paths sub_paths);

static void
oxr_source_bind_inputs(struct oxr_logger* log,
                       struct oxr_session* sess,
                       struct oxr_action* act,
                       struct oxr_source_cache* cache,
                       enum sub_action_path sub_path);

static XrResult
oxr_source_destroy_cb(struct oxr_logger* log, struct oxr_handle_base* hb);

static XrResult
oxr_source_create(struct oxr_logger* log,
                  struct oxr_session* sess,
                  struct oxr_action* act,
                  struct oxr_source** out_src);


/*
 *
 * Action set functions
 *
 */

static XrResult
oxr_action_set_destroy_cb(struct oxr_logger* log, struct oxr_handle_base* hb)
{
	//! @todo Move to oxr_objects.h
	struct oxr_action_set* act_set = (struct oxr_action_set*)hb;

	free(act_set);

	return XR_SUCCESS;
}

XrResult
oxr_action_set_create(struct oxr_logger* log,
                      struct oxr_session* sess,
                      const XrActionSetCreateInfo* createInfo,
                      struct oxr_action_set** out_act_set)
{
	// Mod music for all!
	static uint32_t key_gen = 0;

	//! @todo Implement more fully.
	struct oxr_action_set* act_set = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, act_set, OXR_XR_DEBUG_ACTIONSET,
	                              oxr_action_set_destroy_cb, &sess->handle);

	act_set->key = key_gen++;
	act_set->generation = 1;
	act_set->sess = sess;

	*out_act_set = act_set;

	return XR_SUCCESS;
}


/*
 *
 * Action functions
 *
 */

static XrResult
oxr_action_destroy_cb(struct oxr_logger* log, struct oxr_handle_base* hb)
{
	//! @todo Move to oxr_objects.h
	struct oxr_action* act = (struct oxr_action*)hb;

	// Need to keep track of the generation of this
	// action set to rebuild bindings when used.
	act->act_set->generation++;

	free(act);

	return XR_SUCCESS;
}

XrResult
oxr_action_create(struct oxr_logger* log,
                  struct oxr_action_set* act_set,
                  const XrActionCreateInfo* createInfo,
                  struct oxr_action** out_act)
{
	struct oxr_instance* inst = act_set->sess->sys->inst;
	struct oxr_sub_paths sub_paths = {0};

	// Mod music for all!
	static uint32_t key_gen = 0;

	oxr_classify_sub_action_paths(log, inst,
	                              createInfo->countSubactionPaths,
	                              createInfo->subactionPaths, &sub_paths);

	struct oxr_action* act = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, act, OXR_XR_DEBUG_ACTION,
	                              oxr_action_destroy_cb, &act_set->handle);
	act->key = key_gen++;
	act->act_set = act_set;
	act->sub_paths = sub_paths;
	act->action_type = createInfo->actionType;

	strncpy(act->name, createInfo->actionName, sizeof(act->name));

	// Need to keep track of the generation of this
	// action set to rebuild bindings when used.
	act_set->generation++;

	*out_act = act;

	return XR_SUCCESS;
}


/*
 *
 * "Exproted" helper functions.
 *
 */

void
oxr_classify_sub_action_paths(struct oxr_logger* log,
                              struct oxr_instance* inst,
                              uint32_t num_subaction_paths,
                              const XrPath* subaction_paths,
                              struct oxr_sub_paths* sub_paths)
{
	const char* str = NULL;
	size_t length = 0;

	// Reset the sub_paths completely.
	U_ZERO(sub_paths);

	if (num_subaction_paths == 0) {
		sub_paths->any = true;
		return;
	}

	for (uint32_t i = 0; i < num_subaction_paths; i++) {
		XrPath path = subaction_paths[i];

		if (path == XR_NULL_PATH) {
			sub_paths->any = true;
		} else if (path == inst->path_cache.user) {
			sub_paths->user = true;
		} else if (path == inst->path_cache.head) {
			sub_paths->head = true;
		} else if (path == inst->path_cache.left) {
			sub_paths->left = true;
		} else if (path == inst->path_cache.right) {
			sub_paths->right = true;
		} else if (path == inst->path_cache.gamepad) {
			sub_paths->gamepad = true;
		} else {
			oxr_path_get_string(log, inst, path, &str, &length);

			oxr_warn(log, " unrecognized sub action path '%s'",
			         str);
		}
	}
}

XrResult
oxr_source_get_pose_input(struct oxr_logger* log,
                          struct oxr_session* sess,
                          uint32_t act_key,
                          const struct oxr_sub_paths* sub_paths,
                          struct oxr_source_input** out_input)
{
	struct oxr_source* src = NULL;

	oxr_session_get_source(sess, act_key, &src);

	if (src == NULL) {
		return XR_SUCCESS;
	}

	// Priority of inputs.
	if (src->head.current.active && (sub_paths->head || sub_paths->any)) {
		*out_input = src->head.inputs;
		return XR_SUCCESS;
	}
	if (src->left.current.active && (sub_paths->left || sub_paths->any)) {
		*out_input = src->left.inputs;
		return XR_SUCCESS;
	}
	if (src->right.current.active && (sub_paths->right || sub_paths->any)) {
		*out_input = src->right.inputs;
		return XR_SUCCESS;
	}
	if (src->gamepad.current.active &&
	    (sub_paths->gamepad || sub_paths->any)) {
		*out_input = src->gamepad.inputs;
		return XR_SUCCESS;
	}
	if (src->user.current.active && (sub_paths->user || sub_paths->any)) {
		*out_input = src->user.inputs;
		return XR_SUCCESS;
	}

	return XR_SUCCESS;
}


/*
 *
 * MEGA HACK Functions!
 *
 */

static void
MEGA_HACK_get_binding(struct oxr_logger* log,
                      struct oxr_session* sess,
                      struct oxr_action* act,
                      enum sub_action_path sub_path,
                      struct oxr_source_input inputs[16],
                      uint32_t* num_inputs,
                      struct oxr_source_output outputs[16],
                      uint32_t* num_outputs)
{
	struct xrt_input* input = NULL;
	struct xrt_output* output = NULL;
	struct xrt_device* xdev = NULL;

	switch (sub_path) {
	case OXR_SUB_ACTION_PATH_USER: xdev = NULL; break;
	case OXR_SUB_ACTION_PATH_HEAD: xdev = sess->sys->head; break;
	case OXR_SUB_ACTION_PATH_LEFT: xdev = sess->sys->left; break;
	case OXR_SUB_ACTION_PATH_RIGHT: xdev = sess->sys->right; break;
	case OXR_SUB_ACTION_PATH_GAMEPAD: xdev = NULL; break;
	default: break;
	}

	if (strcmp(act->name, "grip_object") == 0) {
		oxr_xdev_find_input(xdev, XRT_INPUT_PSMV_TRIGGER_VALUE,
		                    &input) ||
		    oxr_xdev_find_input(xdev, XRT_INPUT_HYDRA_TRIGGER_VALUE,
		                        &input);
	} else if (strcmp(act->name, "hand_pose") == 0) {
		oxr_xdev_find_input(xdev, XRT_INPUT_PSMV_BODY_CENTER_POSE,
		                    &input) ||
		    oxr_xdev_find_input(xdev, XRT_INPUT_HYDRA_POSE, &input);
	} else if (strcmp(act->name, "vibrate_hand") == 0) {
		oxr_xdev_find_output(
		    xdev, XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION, &output);
	}

	if (input != NULL) {
		uint32_t index = (*num_inputs)++;
		inputs[index].input = input;
		inputs[index].xdev = xdev;
	}

	if (output != NULL) {
		uint32_t index = (*num_outputs)++;
		outputs[index].name = output->name;
		outputs[index].xdev = xdev;
	}
}


/*
 *
 * Source set functions
 *
 */

static XrResult
oxr_source_set_destroy_cb(struct oxr_logger* log, struct oxr_handle_base* hb)
{
	//! @todo Move to oxr_objects.h
	struct oxr_source_set* src_set = (struct oxr_source_set*)hb;

	free(src_set);

	return XR_SUCCESS;
}

static XrResult
oxr_source_set_create(struct oxr_logger* log,
                      struct oxr_action_set* act_set,
                      struct oxr_source_set** out_src_set)
{
	struct oxr_session* sess = act_set->sess;
	struct oxr_source_set* src_set = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, src_set, OXR_XR_DEBUG_SOURCESET,
	                              oxr_source_set_destroy_cb, &sess->handle);

	src_set->generation = act_set->generation;

	u_hashmap_int_insert(sess->act_sets, act_set->key, src_set);

	*out_src_set = src_set;

	return XR_SUCCESS;
}


/*
 *
 * Source functions
 *
 */

static XrResult
oxr_source_destroy_cb(struct oxr_logger* log, struct oxr_handle_base* hb)
{
	//! @todo Move to oxr_objects.h
	struct oxr_source* src = (struct oxr_source*)hb;

	free(src->user.inputs);
	free(src->user.outputs);
	free(src->head.inputs);
	free(src->head.outputs);
	free(src->left.inputs);
	free(src->left.outputs);
	free(src->right.inputs);
	free(src->right.outputs);
	free(src->gamepad.inputs);
	free(src->gamepad.outputs);
	free(src);

	return XR_SUCCESS;
}

static XrResult
oxr_source_create(struct oxr_logger* log,
                  struct oxr_session* sess,
                  struct oxr_action* act,
                  struct oxr_source** out_src)
{
	struct oxr_source* src = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, src, OXR_XR_DEBUG_SOURCE,
	                              oxr_source_destroy_cb, &sess->handle);

	u_hashmap_int_insert(sess->sources, act->key, src);

	if (act->sub_paths.user || act->sub_paths.any) {
		oxr_source_bind_inputs(log, sess, act, &src->user,
		                       OXR_SUB_ACTION_PATH_USER);
	}

	if (act->sub_paths.head || act->sub_paths.any) {
		oxr_source_bind_inputs(log, sess, act, &src->head,
		                       OXR_SUB_ACTION_PATH_HEAD);
	}

	if (act->sub_paths.left || act->sub_paths.any) {
		oxr_source_bind_inputs(log, sess, act, &src->left,
		                       OXR_SUB_ACTION_PATH_LEFT);
	}

	if (act->sub_paths.right || act->sub_paths.any) {
		oxr_source_bind_inputs(log, sess, act, &src->right,
		                       OXR_SUB_ACTION_PATH_RIGHT);
	}

	if (act->sub_paths.gamepad || act->sub_paths.any) {
		oxr_source_bind_inputs(log, sess, act, &src->gamepad,
		                       OXR_SUB_ACTION_PATH_GAMEPAD);
	}

	*out_src = src;

	return XR_SUCCESS;
}

static void
oxr_source_cache_stop_output(struct oxr_logger* log,
                             struct oxr_session* sess,
                             struct oxr_source_cache* cache)
{
	// Set this as stopped.
	cache->stop_output_time = 0;

	union xrt_output_value value = {0};

	for (uint32_t i = 0; i < cache->num_outputs; i++) {
		struct oxr_source_output* output = &cache->outputs[i];
		struct xrt_device* xdev = output->xdev;

		xdev->set_output(xdev, output->name,
		                 sess->sys->inst->timekeeping, &value);
	}
}

static void
oxr_source_cache_update(struct oxr_logger* log,
                        struct oxr_session* sess,
                        struct oxr_action* act,
                        struct oxr_source_cache* cache,
                        int64_t time,
                        bool selected)
{
	struct oxr_source_state last = cache->current;

	if (!selected) {
		if (cache->stop_output_time > 0) {
			oxr_source_cache_stop_output(log, sess, cache);
		}
		U_ZERO(&cache->current);
		return;
	}

	if (cache->num_outputs > 0 && cache->stop_output_time < time) {
		oxr_source_cache_stop_output(log, sess, cache);
	}

	if (cache->num_inputs > 0) {
		//! @todo Deal with other types and combine sources.
		int64_t timestamp = cache->inputs[0].input->timestamp;
		float value = cache->inputs[0].input->value.vec1.x;

		bool changed = value != last.vec1.x;
		cache->current.vec1.x = value;

		if (last.active && changed) {
			cache->current.timestamp = timestamp;
			cache->current.changed = true;
		} else if (last.active) {
			cache->current.timestamp = last.timestamp;
			cache->current.changed = false;
		} else {
			cache->current.timestamp = timestamp;
			cache->current.changed = false;
		}
	}
}

static void
oxr_source_update(struct oxr_logger* log,
                  struct oxr_session* sess,
                  struct oxr_action* act,
                  int64_t time,
                  struct oxr_sub_paths sub_paths)
{
	struct oxr_source* src = NULL;

	oxr_session_get_source(sess, act->key, &src);
	if (src == NULL) {
		oxr_source_create(log, sess, act, &src);
	}

	if (src == NULL) {
		return;
	}

	bool select_head = sub_paths.head || sub_paths.any;
	bool select_left = sub_paths.left || sub_paths.any;
	bool select_right = sub_paths.right || sub_paths.any;
	bool select_gamepad = sub_paths.gamepad || sub_paths.any;

	oxr_source_cache_update(log, sess, act, &src->head, time, select_head);
	oxr_source_cache_update(log, sess, act, &src->left, time, select_left);
	oxr_source_cache_update(log, sess, act, &src->right, time,
	                        select_right);
	oxr_source_cache_update(log, sess, act, &src->gamepad, time,
	                        select_gamepad);
}

static void
oxr_source_bind_inputs(struct oxr_logger* log,
                       struct oxr_session* sess,
                       struct oxr_action* act,
                       struct oxr_source_cache* cache,
                       enum sub_action_path sub_path)
{
	struct oxr_source_input inputs[16] = {0};
	uint32_t num_inputs = 0;
	struct oxr_source_output outputs[16] = {0};
	uint32_t num_outputs = 0;

	MEGA_HACK_get_binding(log, sess, act, sub_path, inputs, &num_inputs,
	                      outputs, &num_outputs);

	cache->current.active = false;

	if (num_inputs > 0) {
		cache->current.active = true;
		cache->inputs =
		    U_TYPED_ARRAY_CALLOC(struct oxr_source_input, num_inputs);
		for (uint32_t i = 0; i < num_inputs; i++) {
			cache->inputs[i] = inputs[i];
		}
		cache->num_inputs = num_inputs;
	}

	if (num_outputs > 0) {
		cache->current.active = true;
		cache->outputs =
		    U_TYPED_ARRAY_CALLOC(struct oxr_source_output, num_outputs);
		for (uint32_t i = 0; i < num_outputs; i++) {
			cache->outputs[i] = outputs[i];
		}
		cache->num_outputs = num_outputs;
	}
}


/*
 *
 * Session functions.
 *
 */

static void
oxr_session_get_source_set(struct oxr_session* sess,
                           XrActionSet actionSet,
                           struct oxr_source_set** src_set,
                           struct oxr_action_set** act_set)
{
	void* ptr = NULL;
	*act_set = (struct oxr_action_set*)actionSet;

	int ret = u_hashmap_int_find(sess->act_sets, (*act_set)->key, &ptr);
	if (ret == 0) {
		*src_set = (struct oxr_source_set*)ptr;
	}
}

static void
oxr_session_get_source(struct oxr_session* sess,
                       uint32_t act_key,
                       struct oxr_source** out_src)
{
	void* ptr = NULL;

	int ret = u_hashmap_int_find(sess->sources, act_key, &ptr);
	if (ret == 0) {
		*out_src = (struct oxr_source*)ptr;
	}
}

/*!
 * Loop over all actions in a action set and destroy the sources linked to those
 * actions, this is so we can regenerate the bindings.
 */
static void
oxr_session_destroy_all_sources(struct oxr_logger* log,
                                struct oxr_session* sess,
                                struct oxr_action_set* act_set)
{
	for (uint32_t k = 0; k < ARRAY_SIZE(act_set->handle.children); k++) {
		// This assumes that all children are actions.
		struct oxr_action* act =
		    (struct oxr_action*)act_set->handle.children[k];

		if (act == NULL) {
			continue;
		}

		struct oxr_source* src = NULL;
		oxr_session_get_source(sess, act->key, &src);
		if (src == NULL) {
			continue;
		}

		u_hashmap_int_erase(sess->sources, act->key);
		oxr_handle_destroy(log, &src->handle);
	}
}

XrResult
oxr_action_sync_data(struct oxr_logger* log,
                     struct oxr_session* sess,
                     uint32_t countActionSets,
                     const XrActiveActionSet* actionSets)
{
	struct oxr_action_set* act_set = NULL;
	struct oxr_source_set* src_set = NULL;

	// Synchronize outputs to this time.
	int64_t now = time_state_get_now(sess->sys->inst->timekeeping);

	oxr_xdev_update(sess->sys->head, sess->sys->inst->timekeeping);
	oxr_xdev_update(sess->sys->left, sess->sys->inst->timekeeping);
	oxr_xdev_update(sess->sys->right, sess->sys->inst->timekeeping);

	//! @todo These semantics below are all wrong!

	// Has any of the bound action sets been updated.
	for (uint32_t i = 0; i < countActionSets; i++) {
		oxr_session_get_source_set(sess, actionSets[i].actionSet,
		                           &src_set, &act_set);

		// No source set found, create it. This will set it to the
		// current action set generation, that's okay since we will
		// be creating the sources for the actions below.
		if (src_set == NULL) {
			oxr_source_set_create(log, act_set, &src_set);
			continue;
		}

		// Generations match, nothing to do.
		if (src_set->generation == act_set->generation) {
			continue;
		}

		// Found possible out of date bindings, we need to redo these
		// bindings. Destroy the actions currently on this action set.
		oxr_session_destroy_all_sources(log, sess, act_set);
	}

	// Go over all action sets and update them.
	for (uint32_t i = 0; i < countActionSets; i++) {
		struct oxr_sub_paths sub_paths;
		struct oxr_action_set* act_set =
		    (struct oxr_action_set*)actionSets[i].actionSet;

		oxr_classify_sub_action_paths(log, sess->sys->inst, 1,
		                              &actionSets[i].subactionPath,
		                              &sub_paths);

		for (uint32_t k = 0; k < ARRAY_SIZE(act_set->handle.children);
		     k++) {
			// This assumes that all children of a
			// action set are actions.
			struct oxr_action* act =
			    (struct oxr_action*)act_set->handle.children[k];

			if (act == NULL) {
				continue;
			}

			oxr_source_update(log, sess, act, now, sub_paths);
		}
	}

	//! @todo Implement
	return XR_SUCCESS;
}

XrResult
oxr_action_set_interaction_profile_suggested_bindings(
    struct oxr_logger* log,
    struct oxr_session* sess,
    const XrInteractionProfileSuggestedBinding* suggestedBindings)
{
	//! @todo Implement
	struct oxr_instance* inst = sess->sys->inst;
	const char* str;
	size_t length;

#if 0
	fprintf(stderr, "%s\n", __func__);

	oxr_path_get_string(log, inst, suggestedBindings->interactionProfile,
	                    &str, &length);
	fprintf(stderr, "\tinteractionProfile: %s\n", str);
#endif

	for (size_t i = 0; i < suggestedBindings->countSuggestedBindings; i++) {
		const XrActionSuggestedBinding* s =
		    &suggestedBindings->suggestedBindings[i];
		struct oxr_action* act = (struct oxr_action*)s->action;
#if 0
		oxr_path_get_string(log, inst, s->binding, &str, &length);
		fprintf(stderr, "\t\t%s -> %s\n", act->name, str);
#else
		(void)inst;
		(void)act;
		(void)str;
		(void)length;
#endif
	}

	return XR_SUCCESS;
}

XrResult
oxr_action_get_current_interaction_profile(
    struct oxr_logger* log,
    struct oxr_session* sess,
    XrPath topLevelUserPath,
    XrInteractionProfileInfo* interactionProfile)
{
	//! @todo Implement
	return oxr_error(log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_action_get_input_source_localized_name(
    struct oxr_logger* log,
    struct oxr_session* sess,
    XrPath source,
    XrInputSourceLocalizedNameFlags whichComponents,
    uint32_t bufferCapacityInput,
    uint32_t* bufferCountOutput,
    char* buffer)
{
	//! @todo Implement
	return oxr_error(log, XR_ERROR_HANDLE_INVALID, " not implemented");
}


/*
 *
 * Action get functions.
 *
 */

static void
get_state_from_state_bool(struct oxr_source_state* state,
                          XrActionStateBoolean* data)
{
	data->currentState = state->boolean;
	data->lastChangeTime = state->timestamp;
	data->isActive = XR_TRUE;

#if 0
	bool value = state->boolean;

	if (data->isActive) {
		data->currentState |= value;
		data->lastChangeTime = state->timestamp;
		data->isActive = XR_TRUE;
	}
#endif
}

static void
get_state_from_state_vec1(struct oxr_source_state* state,
                          XrActionStateVector1f* data)
{
	data->currentState = state->vec1.x;
	data->lastChangeTime = state->timestamp;
	data->isActive = XR_TRUE;

#if 0
	float value = state->vec1.x;

	if (!data->isActive || (data->isActive && value > data->currentState)) {
		data->currentState = value;
		data->lastChangeTime = state->timestamp;
		data->isActive = XR_TRUE;
	}
#endif
}

static void
get_state_from_state_vec2(struct oxr_source_state* state,
                          XrActionStateVector2f* data)
{
	data->currentState.x = state->vec2.x;
	data->currentState.y = state->vec2.y;
	data->lastChangeTime = state->timestamp;
	data->isActive = XR_TRUE;

#if 0
	float value_x = state->vec2.x;
	float value_y = state->vec2.y;
	float distance = value_x * value_x + value_y * value_y;
	float old_distance =
	    data->isActive ? data->currentState.x * data->currentState.x +
	                         data->currentState.y * data->currentState.y
	                   : 0.f;

	if (!data->isActive || (data->isActive && distance > old_distance)) {
		data->currentState.x = value_x;
		data->currentState.y = value_y;
		data->lastChangeTime = state->timestamp;
		data->isActive = XR_TRUE;
	}
#endif
}

#define OXR_ACTION_GET_FILLER(TYPE)                                            \
	if (sub_paths.user && src->user.current.active) {                      \
		get_state_from_state_##TYPE(&src->user.current, data);         \
	}                                                                      \
	if (sub_paths.head && src->head.current.active) {                      \
		get_state_from_state_##TYPE(&src->head.current, data);         \
	}                                                                      \
	if (sub_paths.left && src->left.current.active) {                      \
		get_state_from_state_##TYPE(&src->left.current, data);         \
	}                                                                      \
	if (sub_paths.right && src->right.current.active) {                    \
		get_state_from_state_##TYPE(&src->right.current, data);        \
	}                                                                      \
	if (sub_paths.gamepad && src->gamepad.current.active) {                \
		get_state_from_state_##TYPE(&src->gamepad.current, data);      \
	}

XrResult
oxr_action_get_boolean(struct oxr_logger* log,
                       struct oxr_action* act,
                       struct oxr_sub_paths sub_paths,
                       XrActionStateBoolean* data)
{
	struct oxr_session* sess = act->act_set->sess;
	struct oxr_source* src = NULL;

	oxr_session_get_source(sess, act->key, &src);

	data->isActive = XR_FALSE;
	U_ZERO(&data->currentState);

	if (src == NULL) {
		return XR_SUCCESS;
	}

	//! @todo support any subpath.
	if (sub_paths.any) {
		return oxr_error(log, XR_ERROR_HANDLE_INVALID,
		                 "any path not implemented!");
	}

	OXR_ACTION_GET_FILLER(bool);

	return XR_SUCCESS;
}

XrResult
oxr_action_get_vector1f(struct oxr_logger* log,
                        struct oxr_action* act,
                        struct oxr_sub_paths sub_paths,
                        XrActionStateVector1f* data)
{
	struct oxr_session* sess = act->act_set->sess;
	struct oxr_source* src = NULL;

	oxr_session_get_source(sess, act->key, &src);

	data->isActive = XR_FALSE;
	U_ZERO(&data->currentState);

	if (src == NULL) {
		return XR_SUCCESS;
	}

	//! @todo support any subpath.
	if (sub_paths.any) {
		return oxr_error(log, XR_ERROR_HANDLE_INVALID,
		                 "any path not implemented!");
	}

	OXR_ACTION_GET_FILLER(vec1);

	return XR_SUCCESS;
}

XrResult
oxr_action_get_vector2f(struct oxr_logger* log,
                        struct oxr_action* act,
                        struct oxr_sub_paths sub_paths,
                        XrActionStateVector2f* data)
{
	struct oxr_session* sess = act->act_set->sess;
	struct oxr_source* src = NULL;

	oxr_session_get_source(sess, act->key, &src);

	data->isActive = XR_FALSE;
	U_ZERO(&data->currentState);

	if (src == NULL) {
		return XR_SUCCESS;
	}

	//! @todo support any subpath.
	if (sub_paths.any) {
		return oxr_error(log, XR_ERROR_HANDLE_INVALID,
		                 "any path not implemented!");
	}

	OXR_ACTION_GET_FILLER(vec2);

	return XR_SUCCESS;
}

XrResult
oxr_action_get_pose(struct oxr_logger* log,
                    struct oxr_action* act,
                    struct oxr_sub_paths sub_paths,
                    XrActionStatePose* data)
{
	struct oxr_session* sess = act->act_set->sess;
	struct oxr_source* src = NULL;

	oxr_session_get_source(sess, act->key, &src);

	data->isActive = XR_FALSE;

	if (src == NULL) {
		return XR_SUCCESS;
	}

	if (sub_paths.user || sub_paths.any) {
		data->isActive |= src->user.current.active;
	}
	if (sub_paths.head || sub_paths.any) {
		data->isActive |= src->head.current.active;
	}
	if (sub_paths.left || sub_paths.any) {
		data->isActive |= src->left.current.active;
	}
	if (sub_paths.right || sub_paths.any) {
		data->isActive |= src->right.current.active;
	}
	if (sub_paths.gamepad || sub_paths.any) {
		data->isActive |= src->gamepad.current.active;
	}

	return XR_SUCCESS;
}

XrResult
oxr_action_get_bound_sources(struct oxr_logger* log,
                             struct oxr_action* act,
                             uint32_t sourceCapacityInput,
                             uint32_t* sourceCountOutput,
                             XrPath* sources)
{
	//! @todo Implement
	return oxr_error(log, XR_ERROR_HANDLE_INVALID, " not implemented");
}


/*
 *
 * Haptic feedback functions.
 *
 */

static void
set_source_output_vibration(struct oxr_session* sess,
                            struct oxr_source_cache* cache,
                            int64_t stop,
                            const XrHapticVibration* data)
{
	cache->stop_output_time = stop;

	union xrt_output_value value = {0};
	value.vibration.frequency = data->frequency;
	value.vibration.amplitude = data->amplitude;

	for (uint32_t i = 0; i < cache->num_outputs; i++) {
		struct oxr_source_output* output = &cache->outputs[i];
		struct xrt_device* xdev = output->xdev;

		xdev->set_output(xdev, output->name,
		                 sess->sys->inst->timekeeping, &value);
	}
}

XrResult
oxr_action_apply_haptic_feedback(struct oxr_logger* log,
                                 struct oxr_action* act,
                                 uint32_t countSubactionPaths,
                                 const XrPath* subactionPaths,
                                 const XrHapticBaseHeader* hapticEvent)
{
	struct oxr_session* sess = act->act_set->sess;
	struct oxr_source* src = NULL;
	struct oxr_sub_paths sub_paths = {0};

	oxr_classify_sub_action_paths(log, sess->sys->inst, countSubactionPaths,
	                              subactionPaths, &sub_paths);

	oxr_session_get_source(sess, act->key, &src);

	if (src == NULL) {
		return XR_SUCCESS;
	}

	const XrHapticVibration* data = (const XrHapticVibration*)hapticEvent;

	int64_t now = time_state_get_now(sess->sys->inst->timekeeping);
	int64_t stop = data->duration <= 0 ? now : now + data->duration;

	// clang-format off
	if (src->user.current.active && (sub_paths.user || sub_paths.any)) {
		set_source_output_vibration(sess, &src->user, stop, data);
	}
	if (src->head.current.active && (sub_paths.head || sub_paths.any)) {
		set_source_output_vibration(sess, &src->head, stop, data);
	}
	if (src->left.current.active && (sub_paths.left || sub_paths.any)) {
		set_source_output_vibration(sess, &src->left, stop, data);
	}
	if (src->right.current.active && (sub_paths.right || sub_paths.any)) {
		set_source_output_vibration(sess, &src->right, stop, data);
	}
	if (src->gamepad.current.active && (sub_paths.gamepad || sub_paths.any)) {
		set_source_output_vibration(sess, &src->gamepad, stop, data);
	}
	// clang-format on

	return XR_SUCCESS;
}

XrResult
oxr_action_stop_haptic_feedback(struct oxr_logger* log,
                                struct oxr_action* act,
                                uint32_t countSubactionPaths,
                                const XrPath* subactionPaths)
{
	struct oxr_session* sess = act->act_set->sess;
	struct oxr_source* src = NULL;
	struct oxr_sub_paths sub_paths = {0};

	oxr_classify_sub_action_paths(log, sess->sys->inst, countSubactionPaths,
	                              subactionPaths, &sub_paths);

	oxr_session_get_source(sess, act->key, &src);

	if (src == NULL) {
		return XR_SUCCESS;
	}

	// clang-format off
	if (src->user.current.active && (sub_paths.user || sub_paths.any)) {
		oxr_source_cache_stop_output(log, sess, &src->user);
	}
	if (src->head.current.active && (sub_paths.head || sub_paths.any)) {
		oxr_source_cache_stop_output(log, sess, &src->head);
	}
	if (src->left.current.active && (sub_paths.left || sub_paths.any)) {
		oxr_source_cache_stop_output(log, sess, &src->left);
	}
	if (src->right.current.active && (sub_paths.right || sub_paths.any)) {
		oxr_source_cache_stop_output(log, sess, &src->right);
	}
	if (src->gamepad.current.active && (sub_paths.gamepad || sub_paths.any)) {
		oxr_source_cache_stop_output(log, sess, &src->gamepad);
	}
	// clang-format on

	return XR_SUCCESS;
}
