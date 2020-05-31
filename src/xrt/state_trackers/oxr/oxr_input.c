// Copyright 2018-2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds input related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "util/u_debug.h"
#include "util/u_time.h"
#include "util/u_misc.h"

#include "xrt/xrt_compiler.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_handle.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


/*
 *
 * Pre declare functions.
 *
 */

static void
oxr_session_get_source_set(struct oxr_session *sess,
                           XrActionSet actionSet,
                           struct oxr_source_set **src_set,
                           struct oxr_action_set **act_set);

static void
oxr_session_get_source(struct oxr_session *sess,
                       uint32_t act_key,
                       struct oxr_source **out_src);

static void
oxr_source_cache_update(struct oxr_logger *log,
                        struct oxr_session *sess,
                        struct oxr_source_cache *cache,
                        int64_t time,
                        bool select);

static void
oxr_source_update(struct oxr_logger *log,
                  struct oxr_session *sess,
                  struct oxr_source *src,
                  int64_t time,
                  struct oxr_sub_paths sub_paths);

static void
oxr_source_bind_inputs(struct oxr_logger *log,
                       struct oxr_sink_logger *slog,
                       struct oxr_session *sess,
                       struct oxr_action *act,
                       struct oxr_source_cache *cache,
                       struct oxr_interaction_profile *profile,
                       enum oxr_sub_action_path sub_path);

static XrResult
oxr_source_destroy_cb(struct oxr_logger *log, struct oxr_handle_base *hb);

static XrResult
oxr_source_create(struct oxr_logger *log,
                  struct oxr_source_set *src_set,
                  struct oxr_action *act,
                  struct oxr_interaction_profile *head,
                  struct oxr_interaction_profile *left,
                  struct oxr_interaction_profile *right,
                  struct oxr_interaction_profile *gamepad);


/*
 *
 * Action set functions
 *
 */

static XrResult
oxr_action_set_destroy_cb(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	//! @todo Move to oxr_objects.h
	struct oxr_action_set *act_set = (struct oxr_action_set *)hb;

	if (act_set->name_item != NULL) {
		u_hashset_erase_item(act_set->inst->action_sets.name_store,
		                     act_set->name_item);
		free(act_set->name_item);
		act_set->name_item = NULL;
	}
	if (act_set->loc_item != NULL) {
		u_hashset_erase_item(act_set->inst->action_sets.loc_store,
		                     act_set->loc_item);
		free(act_set->loc_item);
		act_set->loc_item = NULL;
	}

	u_hashset_destroy(&act_set->actions.name_store);
	u_hashset_destroy(&act_set->actions.loc_store);

	free(act_set);

	return XR_SUCCESS;
}

XrResult
oxr_action_set_create(struct oxr_logger *log,
                      struct oxr_instance *inst,
                      const XrActionSetCreateInfo *createInfo,
                      struct oxr_action_set **out_act_set)
{
	// Mod music for all!
	static uint32_t key_gen = 1;
	int h_ret;

	//! @todo Implement more fully.
	struct oxr_action_set *act_set = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, act_set, OXR_XR_DEBUG_ACTIONSET,
	                              oxr_action_set_destroy_cb, &inst->handle);

	h_ret = u_hashset_create(&act_set->actions.name_store);
	if (h_ret != 0) {
		oxr_handle_destroy(log, &act_set->handle);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to create name_store hashset");
	}

	h_ret = u_hashset_create(&act_set->actions.loc_store);
	if (h_ret != 0) {
		oxr_handle_destroy(log, &act_set->handle);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to create loc_store hashset");
	}

	act_set->key = key_gen++;

	act_set->inst = inst;
	strncpy(act_set->name, createInfo->actionSetName,
	        sizeof(act_set->name));

	u_hashset_create_and_insert_str_c(inst->action_sets.name_store,
	                                  createInfo->actionSetName,
	                                  &act_set->name_item);
	u_hashset_create_and_insert_str_c(inst->action_sets.loc_store,
	                                  createInfo->localizedActionSetName,
	                                  &act_set->loc_item);

	*out_act_set = act_set;

	return XR_SUCCESS;
}


/*
 *
 * Action functions
 *
 */

static XrResult
oxr_action_destroy_cb(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	//! @todo Move to oxr_objects.h
	struct oxr_action *act = (struct oxr_action *)hb;

	if (act->name_item != NULL) {
		u_hashset_erase_item(act->act_set->actions.name_store,
		                     act->name_item);
		free(act->name_item);
		act->name_item = NULL;
	}
	if (act->loc_item != NULL) {
		u_hashset_erase_item(act->act_set->actions.loc_store,
		                     act->loc_item);
		free(act->loc_item);
		act->loc_item = NULL;
	}

	free(act);

	return XR_SUCCESS;
}

XrResult
oxr_action_create(struct oxr_logger *log,
                  struct oxr_action_set *act_set,
                  const XrActionCreateInfo *createInfo,
                  struct oxr_action **out_act)
{
	struct oxr_instance *inst = act_set->inst;
	struct oxr_sub_paths sub_paths = {0};

	// Mod music for all!
	static uint32_t key_gen = 1;

	oxr_classify_sub_action_paths(log, inst,
	                              createInfo->countSubactionPaths,
	                              createInfo->subactionPaths, &sub_paths);

	struct oxr_action *act = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, act, OXR_XR_DEBUG_ACTION,
	                              oxr_action_destroy_cb, &act_set->handle);
	act->key = key_gen++;
	act->act_set = act_set;
	act->sub_paths = sub_paths;
	act->action_type = createInfo->actionType;

	strncpy(act->name, createInfo->actionName, sizeof(act->name));

	u_hashset_create_and_insert_str_c(act_set->actions.name_store,
	                                  createInfo->actionName,
	                                  &act->name_item);
	u_hashset_create_and_insert_str_c(act_set->actions.loc_store,
	                                  createInfo->localizedActionName,
	                                  &act->loc_item);

	*out_act = act;

	return XR_SUCCESS;
}


/*
 *
 * "Exproted" helper functions.
 *
 */

void
oxr_classify_sub_action_paths(struct oxr_logger *log,
                              struct oxr_instance *inst,
                              uint32_t num_subaction_paths,
                              const XrPath *subaction_paths,
                              struct oxr_sub_paths *sub_paths)
{
	const char *str = NULL;
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
oxr_source_get_pose_input(struct oxr_logger *log,
                          struct oxr_session *sess,
                          uint32_t act_key,
                          const struct oxr_sub_paths *sub_paths,
                          struct oxr_source_input **out_input)
{
	struct oxr_source *src = NULL;

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
 * Not so hack functions.
 *
 */

static bool
do_inputs(struct oxr_binding *bind,
          struct xrt_device *xdev,
          struct oxr_source_input inputs[16],
          uint32_t *num_inputs)
{
	struct xrt_input *input = NULL;
	bool found = false;

	for (size_t i = 0; i < bind->num_inputs; i++) {
		if (oxr_xdev_find_input(xdev, bind->inputs[i], &input)) {
			uint32_t index = (*num_inputs)++;
			inputs[index].input = input;
			inputs[index].xdev = xdev;
			found = true;
		}
	}

	return found;
}

static bool
do_outputs(struct oxr_binding *bind,
           struct xrt_device *xdev,
           struct oxr_source_output outputs[16],
           uint32_t *num_outputs)
{
	struct xrt_output *output = NULL;
	bool found = false;

	for (size_t i = 0; i < bind->num_outputs; i++) {
		if (oxr_xdev_find_output(xdev, bind->outputs[i], &output)) {
			uint32_t index = (*num_outputs)++;
			outputs[index].name = output->name;
			outputs[index].xdev = xdev;
			found = true;
		}
	}

	return found;
}

static bool
do_io_bindings(struct oxr_binding *b,
               struct oxr_action *act,
               struct xrt_device *xdev,
               struct oxr_source_input inputs[16],
               uint32_t *num_inputs,
               struct oxr_source_output outputs[16],
               uint32_t *num_outputs)
{
	bool found = false;

	if (act->action_type == XR_ACTION_TYPE_VIBRATION_OUTPUT) {
		found |= do_outputs(b, xdev, outputs, num_outputs);
	} else {
		found |= do_inputs(b, xdev, inputs, num_inputs);
	}

	return found;
}

static bool
ends_with(const char *str, const char *suffix)
{
	int len = strlen(str);
	int suffix_len = strlen(suffix);

	return (len >= suffix_len) &&
	       (0 == strcmp(str + (len - suffix_len), suffix));
}

static void
oxr_source_cache_determine_redirect(struct oxr_logger *log,
                                    struct oxr_session *sess,
                                    struct oxr_action *act,
                                    struct oxr_source_cache *cache,
                                    XrPath bound_path)
{

	cache->redirect = INPUT_REDIRECT_DEFAULT;

	struct oxr_source_input *input = &cache->inputs[0];
	if (input == NULL)
		return;

	const char *str;
	size_t length;
	oxr_path_get_string(log, sess->sys->inst, bound_path, &str, &length);

	enum xrt_input_type t = XRT_GET_INPUT_TYPE(input->input->name);

	// trackpad/thumbstick data is kept in vec2f values.
	// When a float action binds to ../trackpad/x or ../thumbstick/y, store
	// the information which vec2f component to read.
	if (act->action_type == XR_ACTION_TYPE_FLOAT_INPUT &&
	    t == XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE) {
		if (ends_with(str, "/x")) {
			cache->redirect = INPUT_REDIRECT_VEC2_X_TO_VEC1;
		} else if (ends_with(str, "/y")) {
			cache->redirect = INPUT_REDIRECT_VEC2_Y_TO_VEC1;
		} else {
			oxr_log(log,
			        "No rule to get float from vec2f for action "
			        "%s, binding %s\n",
			        act->name, str);
		}
	}
}

static XrPath
get_matched_xrpath(struct oxr_binding *b, struct oxr_action *act)
{
	XrPath preferred_path = XR_NULL_PATH;
	for (uint32_t i = 0; i < b->num_keys; i++) {
		if (b->keys[i] == act->key) {
			uint32_t preferred_path_index = XR_NULL_PATH;
			preferred_path_index =
			    b->preferred_binding_path_index[i];
			preferred_path = b->paths[preferred_path_index];
			break;
		}
	}
	return preferred_path;
}

static void
get_binding(struct oxr_logger *log,
            struct oxr_sink_logger *slog,
            struct oxr_session *sess,
            struct oxr_action *act,
            struct oxr_interaction_profile *profile,
            enum oxr_sub_action_path sub_path,
            struct oxr_source_input inputs[16],
            uint32_t *num_inputs,
            struct oxr_source_output outputs[16],
            uint32_t *num_outputs,
            XrPath *bound_path)
{
	struct xrt_device *xdev = NULL;
	struct oxr_binding *bindings[32];
	const char *profile_str;
	const char *user_path_str;
	size_t length;

	//! @todo This probably falls on its head if the application doesn't use
	//! sub action paths.
	switch (sub_path) {
	case OXR_SUB_ACTION_PATH_USER:
		user_path_str = "/user";
		xdev = NULL;
		break;
	case OXR_SUB_ACTION_PATH_HEAD:
		user_path_str = "/user/head";
		xdev = sess->sys->head;
		break;
	case OXR_SUB_ACTION_PATH_LEFT:
		user_path_str = "/user/hand/left";
		xdev = sess->sys->left;
		break;
	case OXR_SUB_ACTION_PATH_RIGHT:
		user_path_str = "/user/hand/right";
		xdev = sess->sys->right;
		break;
	case OXR_SUB_ACTION_PATH_GAMEPAD:
		user_path_str = "/user/hand/gamepad";
		xdev = NULL;
		break;
	default: break;
	}

	oxr_slog(slog, "\tFor: %s\n", user_path_str);

	if (xdev == NULL) {
		oxr_slog(slog, "\t\tNo xdev!\n");
		return;
	}

	if (profile == NULL) {
		oxr_slog(slog, "\t\tNo profile!\n");
		return;
	}

	oxr_path_get_string(log, sess->sys->inst, profile->path, &profile_str,
	                    &length);

	oxr_slog(slog, "\t\tProfile: %s\n", profile_str);

	size_t num = 0;
	oxr_binding_find_bindings_from_key(log, profile, act->key, bindings,
	                                   &num);
	if (num == 0) {
		oxr_slog(slog, "\t\tNo bindings\n");
		return;
	}

	for (size_t i = 0; i < num; i++) {
		const char *str = NULL;
		struct oxr_binding *b = bindings[i];

		XrPath matched_path = get_matched_xrpath(b, act);

		oxr_path_get_string(log, sess->sys->inst, matched_path, &str,
		                    &length);
		oxr_slog(slog, "\t\t\tBinding: %s\n", str);

		if (b->sub_path != sub_path) {
			oxr_slog(slog, "\t\t\t\tRejected! (SUB PATH)\n");
			continue;
		}

		bool found = do_io_bindings(b, act, xdev, inputs, num_inputs,
		                            outputs, num_outputs);

		if (found) {
			*bound_path = matched_path;
			oxr_slog(slog, "\t\t\t\tBound!\n");
		} else {
			oxr_slog(slog, "\t\t\t\tRejected! (NO XDEV MAPPING)\n");
		}
	}
}


/*
 *
 * Source set functions
 *
 */

static XrResult
oxr_source_set_destroy_cb(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	//! @todo Move to oxr_objects.h
	struct oxr_source_set *src_set = (struct oxr_source_set *)hb;

	free(src_set);

	return XR_SUCCESS;
}

static XrResult
oxr_source_set_create(struct oxr_logger *log,
                      struct oxr_session *sess,
                      struct oxr_action_set *act_set,
                      struct oxr_source_set **out_src_set)
{
	struct oxr_source_set *src_set = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, src_set, OXR_XR_DEBUG_SOURCESET,
	                              oxr_source_set_destroy_cb, &sess->handle);

	src_set->sess = sess;
	u_hashmap_int_insert(sess->act_sets, act_set->key, src_set);

	src_set->next = sess->src_set_list;
	sess->src_set_list = src_set;

	*out_src_set = src_set;

	return XR_SUCCESS;
}


/*
 *
 * Source functions
 *
 */

static XrResult
oxr_source_destroy_cb(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	//! @todo Move to oxr_objects.h
	struct oxr_source *src = (struct oxr_source *)hb;

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
oxr_source_create(struct oxr_logger *log,
                  struct oxr_source_set *src_set,
                  struct oxr_action *act,
                  struct oxr_interaction_profile *head,
                  struct oxr_interaction_profile *left,
                  struct oxr_interaction_profile *right,
                  struct oxr_interaction_profile *gamepad)
{
	struct oxr_session *sess = src_set->sess;
	struct oxr_source *src = NULL;
	struct oxr_sink_logger slog = {0};
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, src, OXR_XR_DEBUG_SOURCE,
	                              oxr_source_destroy_cb, &src_set->handle);

	u_hashmap_int_insert(src_set->sess->sources, act->key, src);

	// Need to copy this.
	src->action_type = act->action_type;

	// Start logging into a single buffer.
	oxr_slog(&slog, ": Binding %s/%s\n", act->act_set->name, act->name);

	if (act->sub_paths.user || act->sub_paths.any) {
#if 0
		oxr_source_bind_inputs(log, slog, sess, act, &src->user, user,
		                       OXR_SUB_ACTION_PATH_USER);
#endif
	}

	if (act->sub_paths.head || act->sub_paths.any) {
		oxr_source_bind_inputs(log, &slog, sess, act, &src->head, head,
		                       OXR_SUB_ACTION_PATH_HEAD);
	}

	if (act->sub_paths.left || act->sub_paths.any) {
		oxr_source_bind_inputs(log, &slog, sess, act, &src->left, left,
		                       OXR_SUB_ACTION_PATH_LEFT);
	}

	if (act->sub_paths.right || act->sub_paths.any) {
		oxr_source_bind_inputs(log, &slog, sess, act, &src->right,
		                       right, OXR_SUB_ACTION_PATH_RIGHT);
	}

	if (act->sub_paths.gamepad || act->sub_paths.any) {
		oxr_source_bind_inputs(log, &slog, sess, act, &src->gamepad,
		                       gamepad, OXR_SUB_ACTION_PATH_GAMEPAD);
	}

	oxr_slog(&slog, "\tDone");

	// Also frees all data.
	if (sess->sys->inst->debug_bindings) {
		oxr_log_slog(log, &slog);
	} else {
		oxr_slog_abort(&slog);
	}

	return XR_SUCCESS;
}

static void
oxr_source_cache_stop_output(struct oxr_logger *log,
                             struct oxr_session *sess,
                             struct oxr_source_cache *cache)
{
	// Set this as stopped.
	cache->stop_output_time = 0;

	union xrt_output_value value = {0};

	for (uint32_t i = 0; i < cache->num_outputs; i++) {
		struct oxr_source_output *output = &cache->outputs[i];
		struct xrt_device *xdev = output->xdev;

		xrt_device_set_output(xdev, output->name, &value);
	}
}

static void
oxr_source_cache_update(struct oxr_logger *log,
                        struct oxr_session *sess,
                        struct oxr_source_cache *cache,
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

	if (cache->num_outputs > 0) {
		cache->current.active = true;
		if (cache->stop_output_time < time) {
			oxr_source_cache_stop_output(log, sess, cache);
		}
	}

	if (cache->num_inputs > 0) {

		/*!
		 * @todo This logic should be a lot more smarter.
		 */

		// If the input is not active signal that.
		if (!cache->inputs[0].input->active) {
			// Reset all state.
			U_ZERO(&cache->current);
			return;
		}

		// Signal that the input is active, always set just to be sure.
		cache->current.active = true;

		/*!
		 * @todo Combine multiple sources for a single subaction path
		 * and convert type as required.
		 */

		struct xrt_input *input = cache->inputs[0].input;
		int64_t timestamp = input->timestamp;
		bool changed = false;
		switch (XRT_GET_INPUT_TYPE(input->name)) {
		case XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE:
		case XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE: {
			changed = (input->value.vec1.x != last.vec1.x);
			cache->current.vec1.x = input->value.vec1.x;
			break;
		}
		case XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE: {
			changed = (input->value.vec2.x != last.vec2.x) ||
			          (input->value.vec2.y != last.vec2.y);
			cache->current.vec2.x = input->value.vec2.x;
			cache->current.vec2.y = input->value.vec2.y;
			break;
		}
#if 0
		case XRT_INPUT_TYPE_VEC3_MINUS_ONE_TO_ONE: {
			changed = (input->value.vec3.x != last.vec3.x) ||
			          (input->value.vec3.y != last.vec3.y) ||
			          (input->value.vec3.z != last.vec3.z);
			cache->current.vec3.x = input->value.vec3.x;
			cache->current.vec3.y = input->value.vec3.y;
			cache->current.vec3.z = input->value.vec3.z;
			break;
		}
#endif
		case XRT_INPUT_TYPE_BOOLEAN: {
			changed = (input->value.boolean != last.boolean);
			cache->current.boolean = input->value.boolean;
			break;
		}
		case XRT_INPUT_TYPE_POSE: return;
		default:
			// Should not end up here.
			assert(false);
		}

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

#define BOOL_CHECK(NAME)                                                       \
	if (src->NAME.current.active) {                                        \
		active |= true;                                                \
		value |= src->NAME.current.boolean;                            \
		timestamp = src->NAME.current.timestamp;                       \
	}
#define VEC1_CHECK(NAME)                                                       \
	if (src->NAME.current.active) {                                        \
		active |= true;                                                \
		if (value < src->NAME.current.vec1.x) {                        \
			value = src->NAME.current.vec1.x;                      \
			timestamp = src->NAME.current.timestamp;               \
		}                                                              \
	}
#define VEC2_CHECK(NAME)                                                       \
	if (src->NAME.current.active) {                                        \
		active |= true;                                                \
		float curr_x = src->NAME.current.vec2.x;                       \
		float curr_y = src->NAME.current.vec2.y;                       \
		float curr_d = curr_x * curr_x + curr_y * curr_y;              \
		if (distance < curr_d) {                                       \
			x = curr_x;                                            \
			y = curr_y;                                            \
			distance = curr_d;                                     \
			timestamp = src->NAME.current.timestamp;               \
		}                                                              \
	}

static void
oxr_source_update(struct oxr_logger *log,
                  struct oxr_session *sess,
                  struct oxr_source *src,
                  int64_t time,
                  struct oxr_sub_paths sub_paths)
{
	// This really shouldn't be happening.
	if (src == NULL) {
		return;
	}

	//! @todo "/user" sub-action path.

	bool select_any = sub_paths.any;
	bool select_head = sub_paths.head || sub_paths.any;
	bool select_left = sub_paths.left || sub_paths.any;
	bool select_right = sub_paths.right || sub_paths.any;
	bool select_gamepad = sub_paths.gamepad || sub_paths.any;

	// clang-format off
	oxr_source_cache_update(log, sess, &src->head, time, select_head);
	oxr_source_cache_update(log, sess, &src->left, time, select_left);
	oxr_source_cache_update(log, sess, &src->right, time, select_right);
	oxr_source_cache_update(log, sess, &src->gamepad, time, select_gamepad);
	// clang-format on

	if (!select_any) {
		U_ZERO(&src->any_state);
		return;
	}

	/*
	 * Any state.
	 */
	struct oxr_source_state last = src->any_state;
	bool active = false;
	bool changed = false;
	XrTime timestamp = 0;

	switch (src->action_type) {
	case XR_ACTION_TYPE_BOOLEAN_INPUT: {
		bool value = false;
		BOOL_CHECK(user);
		BOOL_CHECK(head);
		BOOL_CHECK(left);
		BOOL_CHECK(right);
		BOOL_CHECK(gamepad);

		changed = last.boolean != value;
		src->any_state.boolean = value;
		break;
	}
	case XR_ACTION_TYPE_FLOAT_INPUT: {
		float value = -2.0;
		VEC1_CHECK(user);
		VEC1_CHECK(head);
		VEC1_CHECK(left);
		VEC1_CHECK(right);
		VEC1_CHECK(gamepad);

		changed = last.vec1.x != value;
		src->any_state.vec1.x = value;
		break;
	}
	case XR_ACTION_TYPE_VECTOR2F_INPUT: {
		float x = 0.0;
		float y = 0.0;
		float distance = -1.0;
		VEC2_CHECK(user);
		VEC2_CHECK(head);
		VEC2_CHECK(left);
		VEC2_CHECK(right);
		VEC2_CHECK(gamepad);

		changed = last.vec2.x != x || last.vec2.y != y;
		src->any_state.vec2.x = x;
		src->any_state.vec2.y = y;
		break;
	}
	default:
	case XR_ACTION_TYPE_POSE_INPUT:
	case XR_ACTION_TYPE_VIBRATION_OUTPUT:
		// Nothing to do
		//! @todo You sure?
		return;
	}

	if (!active) {
		U_ZERO(&src->any_state);
	} else if (last.active && changed) {
		src->any_state.timestamp = timestamp;
		src->any_state.changed = true;
		src->any_state.active = true;
	} else if (last.active) {
		src->any_state.timestamp = last.timestamp;
		src->any_state.changed = false;
		src->any_state.active = true;
	} else {
		src->any_state.timestamp = timestamp;
		src->any_state.changed = false;
		src->any_state.active = true;
	}
}

static void
oxr_source_bind_inputs(struct oxr_logger *log,
                       struct oxr_sink_logger *slog,
                       struct oxr_session *sess,
                       struct oxr_action *act,
                       struct oxr_source_cache *cache,
                       struct oxr_interaction_profile *profile,
                       enum oxr_sub_action_path sub_path)
{
	struct oxr_source_input inputs[16] = {0};
	uint32_t num_inputs = 0;
	struct oxr_source_output outputs[16] = {0};
	uint32_t num_outputs = 0;

	//! @todo Should this be asserted to be none-null?
	XrPath bound_path = XR_NULL_PATH;
	get_binding(log, slog, sess, act, profile, sub_path, inputs,
	            &num_inputs, outputs, &num_outputs, &bound_path);

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

	oxr_source_cache_determine_redirect(log, sess, act, cache, bound_path);
}


/*
 *
 * Session functions.
 *
 */

static void
oxr_session_get_source_set(struct oxr_session *sess,
                           XrActionSet actionSet,
                           struct oxr_source_set **src_set,
                           struct oxr_action_set **act_set)
{
	void *ptr = NULL;
	*act_set =
	    XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_action_set *, actionSet);

	int ret = u_hashmap_int_find(sess->act_sets, (*act_set)->key, &ptr);
	if (ret == 0) {
		*src_set = (struct oxr_source_set *)ptr;
	}
}

static void
oxr_session_get_source(struct oxr_session *sess,
                       uint32_t act_key,
                       struct oxr_source **out_src)
{
	void *ptr = NULL;

	int ret = u_hashmap_int_find(sess->sources, act_key, &ptr);
	if (ret == 0) {
		*out_src = (struct oxr_source *)ptr;
	}
}

XrResult
oxr_session_attach_action_sets(struct oxr_logger *log,
                               struct oxr_session *sess,
                               const XrSessionActionSetsAttachInfo *bindInfo)
{
	struct oxr_instance *inst = sess->sys->inst;
	struct oxr_interaction_profile *head = NULL;
	struct oxr_interaction_profile *left = NULL;
	struct oxr_interaction_profile *right = NULL;
	struct oxr_action_set *act_set = NULL;
	struct oxr_source_set *src_set = NULL;
	struct oxr_action *act = NULL;

	oxr_find_profile_for_device(log, inst, sess->sys->head, &head);
	oxr_find_profile_for_device(log, inst, sess->sys->left, &left);
	oxr_find_profile_for_device(log, inst, sess->sys->right, &right);

	// Has any of the bound action sets been updated.
	for (uint32_t i = 0; i < bindInfo->countActionSets; i++) {
		act_set = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_action_set *,
		                                     bindInfo->actionSets[i]);
		act_set->attached = true;

		oxr_source_set_create(log, sess, act_set, &src_set);

		for (uint32_t k = 0; k < XRT_MAX_HANDLE_CHILDREN; k++) {
			act = (struct oxr_action *)act_set->handle.children[k];
			if (act == NULL) {
				continue;
			}

			oxr_source_create(log, src_set, act, head, left, right,
			                  NULL);
		}
	}

	if (head != NULL) {
		sess->head = head->path;
	}
	if (left != NULL) {
		sess->left = left->path;
	}
	if (right != NULL) {
		sess->right = right->path;
	}

	sess->actionsAttached = true;

	return oxr_session_success_result(sess);
}

XrResult
oxr_action_sync_data(struct oxr_logger *log,
                     struct oxr_session *sess,
                     uint32_t countActionSets,
                     const XrActiveActionSet *actionSets)
{
	struct oxr_action_set *act_set = NULL;
	struct oxr_source_set *src_set = NULL;

	// Check that all action sets has been attached.
	for (uint32_t i = 0; i < countActionSets; i++) {
		oxr_session_get_source_set(sess, actionSets[i].actionSet,
		                           &src_set, &act_set);
		if (src_set == NULL) {
			return oxr_error(
			    log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
			    "(actionSets[%i].actionSet) action set '%s' has "
			    "not been attached to this session",
			    i, act_set != NULL ? act_set->name : "NULL");
		}
	}

	// Synchronize outputs to this time.
	int64_t now = time_state_get_now(sess->sys->inst->timekeeping);

	// Loop over all xdev devices.
	for (size_t i = 0; i < sess->sys->num_xdevs; i++) {
		oxr_xdev_update(sess->sys->xdevs[i]);
	}

	// Reset all requested source sets.
	src_set = sess->src_set_list;
	while (src_set != NULL) {
		U_ZERO(&src_set->requested_sub_paths);

		// Grab the next one.
		src_set = src_set->next;
	}

	// Go over all action sets and update them.
	for (uint32_t i = 0; i < countActionSets; i++) {
		struct oxr_sub_paths sub_paths;
		oxr_session_get_source_set(sess, actionSets[i].actionSet,
		                           &src_set, &act_set);
		assert(src_set != NULL);

		oxr_classify_sub_action_paths(log, sess->sys->inst, 1,
		                              &actionSets[i].subactionPath,
		                              &sub_paths);

		src_set->requested_sub_paths.any |= sub_paths.any;
		src_set->requested_sub_paths.user |= sub_paths.user;
		src_set->requested_sub_paths.head |= sub_paths.head;
		src_set->requested_sub_paths.left |= sub_paths.left;
		src_set->requested_sub_paths.right |= sub_paths.right;
		src_set->requested_sub_paths.gamepad |= sub_paths.gamepad;
	}

	// Reset all source sets.
	src_set = sess->src_set_list;
	while (src_set != NULL) {
		struct oxr_sub_paths sub_paths = src_set->requested_sub_paths;


		for (uint32_t k = 0; k < XRT_MAX_HANDLE_CHILDREN; k++) {
			// This assumes that all children of a
			// source set are actions.
			struct oxr_source *src =
			    (struct oxr_source *)src_set->handle.children[k];

			if (src == NULL) {
				continue;
			}

			oxr_source_update(log, sess, src, now, sub_paths);
		}

		// Grab the next one.
		src_set = src_set->next;
	}


	return oxr_session_success_focused_result(sess);
}


/*
 *
 * Action get functions.
 *
 */

static void
get_state_from_state_bool(struct oxr_source_state *state,
                          XrActionStateBoolean *data,
                          enum xrt_source_value_redirect redirect)
{
	data->currentState = state->boolean;
	data->lastChangeTime = state->timestamp;
	data->changedSinceLastSync = state->changed;
	data->isActive = XR_TRUE;
}

static void
get_state_from_state_vec1(struct oxr_source_state *state,
                          XrActionStateFloat *data,
                          enum xrt_source_value_redirect redirect)
{
	switch (redirect) {
	case INPUT_REDIRECT_VEC2_X_TO_VEC1:
		data->currentState = state->vec2.x;
		break;
	case INPUT_REDIRECT_VEC2_Y_TO_VEC1:
		data->currentState = state->vec2.y;
		break;
	case INPUT_REDIRECT_DEFAULT:
	default: data->currentState = state->vec1.x; break;
	}

	data->lastChangeTime = state->timestamp;
	data->changedSinceLastSync = state->changed;
	data->isActive = XR_TRUE;
}

static void
get_state_from_state_vec2(struct oxr_source_state *state,
                          XrActionStateVector2f *data,
                          enum xrt_source_value_redirect redirect)
{
	data->currentState.x = state->vec2.x;
	data->currentState.y = state->vec2.y;
	data->lastChangeTime = state->timestamp;
	data->changedSinceLastSync = state->changed;
	data->isActive = XR_TRUE;
}

#define OXR_ACTION_GET_FILLER(TYPE)                                            \
	if (sub_paths.any && src->any_state.active) {                          \
		get_state_from_state_##TYPE(&src->any_state, data,             \
		                            INPUT_REDIRECT_DEFAULT);           \
	}                                                                      \
	if (sub_paths.user && src->user.current.active) {                      \
		get_state_from_state_##TYPE(&src->user.current, data,          \
		                            src->user.redirect);               \
	}                                                                      \
	if (sub_paths.head && src->head.current.active) {                      \
		get_state_from_state_##TYPE(&src->head.current, data,          \
		                            src->head.redirect);               \
	}                                                                      \
	if (sub_paths.left && src->left.current.active) {                      \
		get_state_from_state_##TYPE(&src->left.current, data,          \
		                            src->left.redirect);               \
	}                                                                      \
	if (sub_paths.right && src->right.current.active) {                    \
		get_state_from_state_##TYPE(&src->right.current, data,         \
		                            src->right.redirect);              \
	}                                                                      \
	if (sub_paths.gamepad && src->gamepad.current.active) {                \
		get_state_from_state_##TYPE(&src->gamepad.current, data,       \
		                            src->gamepad.redirect);            \
	}


XrResult
oxr_action_get_boolean(struct oxr_logger *log,
                       struct oxr_session *sess,
                       uint64_t key,
                       struct oxr_sub_paths sub_paths,
                       XrActionStateBoolean *data)
{
	struct oxr_source *src = NULL;

	oxr_session_get_source(sess, key, &src);
	if (src == NULL) {
		return oxr_error(
		    log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		    "Action has not been attached to this session");
	}

	data->isActive = XR_FALSE;
	U_ZERO(&data->currentState);


	OXR_ACTION_GET_FILLER(bool);

	return oxr_session_success_result(sess);
}

XrResult
oxr_action_get_vector1f(struct oxr_logger *log,
                        struct oxr_session *sess,
                        uint64_t key,
                        struct oxr_sub_paths sub_paths,
                        XrActionStateFloat *data)
{
	struct oxr_source *src = NULL;

	oxr_session_get_source(sess, key, &src);
	if (src == NULL) {
		return oxr_error(
		    log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		    "Action has not been attached to this session");
	}

	data->isActive = XR_FALSE;
	U_ZERO(&data->currentState);

	OXR_ACTION_GET_FILLER(vec1);

	return oxr_session_success_result(sess);
}

XrResult
oxr_action_get_vector2f(struct oxr_logger *log,
                        struct oxr_session *sess,
                        uint64_t key,
                        struct oxr_sub_paths sub_paths,
                        XrActionStateVector2f *data)
{
	struct oxr_source *src = NULL;

	oxr_session_get_source(sess, key, &src);
	if (src == NULL) {
		return oxr_error(
		    log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		    "Action has not been attached to this session");
	}

	data->isActive = XR_FALSE;
	U_ZERO(&data->currentState);

	OXR_ACTION_GET_FILLER(vec2);

	return oxr_session_success_result(sess);
}

XrResult
oxr_action_get_pose(struct oxr_logger *log,
                    struct oxr_session *sess,
                    uint64_t key,
                    struct oxr_sub_paths sub_paths,
                    XrActionStatePose *data)
{
	struct oxr_source *src = NULL;

	oxr_session_get_source(sess, key, &src);
	if (src == NULL) {
		return oxr_error(
		    log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		    "Action has not been attached to this session");
	}

	data->isActive = XR_FALSE;

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

	return oxr_session_success_result(sess);
}


/*
 *
 * Haptic feedback functions.
 *
 */

static void
set_source_output_vibration(struct oxr_session *sess,
                            struct oxr_source_cache *cache,
                            int64_t stop,
                            const XrHapticVibration *data)
{
	cache->stop_output_time = stop;

	union xrt_output_value value = {0};
	value.vibration.frequency = data->frequency;
	value.vibration.amplitude = data->amplitude;
	value.vibration.duration = data->duration;

	for (uint32_t i = 0; i < cache->num_outputs; i++) {
		struct oxr_source_output *output = &cache->outputs[i];
		struct xrt_device *xdev = output->xdev;

		xrt_device_set_output(xdev, output->name, &value);
	}
}



XrResult
oxr_action_apply_haptic_feedback(struct oxr_logger *log,
                                 struct oxr_session *sess,
                                 uint64_t key,
                                 struct oxr_sub_paths sub_paths,
                                 const XrHapticBaseHeader *hapticEvent)
{
	struct oxr_source *src = NULL;

	oxr_session_get_source(sess, key, &src);
	if (src == NULL) {
		return oxr_error(
		    log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		    "Action has not been attached to this session");
	}

	const XrHapticVibration *data = (const XrHapticVibration *)hapticEvent;

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

	return oxr_session_success_result(sess);
}

XrResult
oxr_action_stop_haptic_feedback(struct oxr_logger *log,
                                struct oxr_session *sess,
                                uint64_t key,
                                struct oxr_sub_paths sub_paths)
{
	struct oxr_source *src = NULL;

	oxr_session_get_source(sess, key, &src);
	if (src == NULL) {
		return oxr_error(
		    log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		    "Action has not been attached to this session");
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

	return oxr_session_success_result(sess);
}
