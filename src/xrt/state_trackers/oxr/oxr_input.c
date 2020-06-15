// Copyright 2018-2020, Collabora, Ltd.
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
#include "oxr_input_transform.h"

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
oxr_session_get_action_set_attachment(
    struct oxr_session *sess,
    XrActionSet actionSet,
    struct oxr_action_set_attachment **act_set_attached,
    struct oxr_action_set **act_set);

static void
oxr_session_get_action_attachment(
    struct oxr_session *sess,
    uint32_t act_key,
    struct oxr_action_attachment **out_act_attached);

static void
oxr_action_cache_update(struct oxr_logger *log,
                        struct oxr_session *sess,
                        struct oxr_action_cache *cache,
                        int64_t time,
                        bool select);

static void
oxr_action_attachment_update(struct oxr_logger *log,
                             struct oxr_session *sess,
                             struct oxr_action_attachment *act_attached,
                             int64_t time,
                             struct oxr_sub_paths sub_paths);

static void
oxr_action_bind_inputs(struct oxr_logger *log,
                       struct oxr_sink_logger *slog,
                       struct oxr_session *sess,
                       struct oxr_action *act,
                       struct oxr_action_cache *cache,
                       struct oxr_interaction_profile *profile,
                       enum oxr_sub_action_path sub_path);

/*
 *
 * Action attachment functions
 *
 */

/*!
 * De-initialize/de-allocate all dynamic members of @ref oxr_action_cache
 * @private @memberof oxr_action_cache
 */
static void
oxr_action_cache_teardown(struct oxr_action_cache *cache)
{
	// Clean up input transforms
	for (uint32_t i = 0; i < cache->num_inputs; i++) {
		struct oxr_action_input *action_input = &cache->inputs[i];
		oxr_input_transform_destroy(&(action_input->transforms));
		action_input->num_transforms = 0;
	}
	free(cache->inputs);
	cache->inputs = NULL;
	free(cache->outputs);
	cache->outputs = NULL;
}

/*!
 * Tear down an action attachment struct.
 *
 * Does not deallocate the struct itself.
 *
 * @public @memberof oxr_action_attachment
 */
static void
oxr_action_attachment_teardown(struct oxr_action_attachment *act_attached)
{
	struct oxr_session *sess = act_attached->sess;
	u_hashmap_int_erase(sess->act_attachments_by_key,
	                    act_attached->act_key);
	oxr_action_cache_teardown(&(act_attached->user));
	oxr_action_cache_teardown(&(act_attached->head));
	oxr_action_cache_teardown(&(act_attached->left));
	oxr_action_cache_teardown(&(act_attached->right));
	oxr_action_cache_teardown(&(act_attached->gamepad));
	// Unref this action's refcounted data
	oxr_refcounted_unref(&act_attached->act_ref->base);
}

/*!
 * Set up an action attachment struct.
 *
 * @public @memberof oxr_action_attachment
 */
static XrResult
oxr_action_attachment_init(struct oxr_logger *log,
                           struct oxr_action_set_attachment *act_set_attached,
                           struct oxr_action_attachment *act_attached,
                           struct oxr_action *act)
{
	struct oxr_session *sess = act_set_attached->sess;
	act_attached->sess = sess;
	act_attached->act_set_attached = act_set_attached;
	u_hashmap_int_insert(sess->act_attachments_by_key, act->act_key,
	                     act_attached);

	// Reference this action's refcounted data
	act_attached->act_ref = act->data;
	oxr_refcounted_ref(&act_attached->act_ref->base);

	// Copy this for efficiency.
	act_attached->act_key = act->act_key;
	return XR_SUCCESS;
}


/*
 *
 * Action set attachment functions
 *
 */

/*!
 * @public @memberof oxr_action_set_attachment
 */
static XrResult
oxr_action_set_attachment_init(
    struct oxr_logger *log,
    struct oxr_session *sess,
    struct oxr_action_set *act_set,
    struct oxr_action_set_attachment *act_set_attached)
{
	act_set_attached->sess = sess;

	// Reference this action set's refcounted data
	act_set_attached->act_set_ref = act_set->data;
	oxr_refcounted_ref(&act_set_attached->act_set_ref->base);

	u_hashmap_int_insert(sess->act_sets_attachments_by_key,
	                     act_set->act_set_key, act_set_attached);

	// Copy this for efficiency.
	act_set_attached->act_set_key = act_set->act_set_key;

	return XR_SUCCESS;
}

void
oxr_action_set_attachment_teardown(
    struct oxr_action_set_attachment *act_set_attached)
{
	for (size_t i = 0; i < act_set_attached->num_action_attachments; ++i) {
		oxr_action_attachment_teardown(
		    &(act_set_attached->act_attachments[i]));
	}
	free(act_set_attached->act_attachments);
	act_set_attached->act_attachments = NULL;
	act_set_attached->num_action_attachments = 0;

	struct oxr_session *sess = act_set_attached->sess;
	u_hashmap_int_erase(sess->act_sets_attachments_by_key,
	                    act_set_attached->act_set_key);

	// Unref this action set's refcounted data
	oxr_refcounted_unref(&act_set_attached->act_set_ref->base);
}


/*
 *
 * Action set functions
 *
 */
static void
oxr_action_set_ref_destroy_cb(struct oxr_refcounted *orc)
{
	struct oxr_action_set_ref *act_set_ref =
	    (struct oxr_action_set_ref *)orc;

	u_hashset_destroy(&act_set_ref->actions.name_store);
	u_hashset_destroy(&act_set_ref->actions.loc_store);

	free(act_set_ref);
}

static XrResult
oxr_action_set_destroy_cb(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	struct oxr_action_set *act_set = (struct oxr_action_set *)hb;

	oxr_refcounted_unref(&act_set->data->base);
	act_set->data = NULL;

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

	struct oxr_action_set *act_set = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, act_set, OXR_XR_DEBUG_ACTIONSET,
	                              oxr_action_set_destroy_cb, &inst->handle);

	struct oxr_action_set_ref *act_set_ref =
	    U_TYPED_CALLOC(struct oxr_action_set_ref);
	act_set_ref->base.destroy = oxr_action_set_ref_destroy_cb;
	oxr_refcounted_ref(&act_set_ref->base);
	act_set->data = act_set_ref;

	act_set_ref->act_set_key = key_gen++;
	act_set->act_set_key = act_set_ref->act_set_key;

	act_set->inst = inst;

	h_ret = u_hashset_create(&act_set_ref->actions.name_store);
	if (h_ret != 0) {
		oxr_handle_destroy(log, &act_set->handle);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to create name_store hashset");
	}

	h_ret = u_hashset_create(&act_set_ref->actions.loc_store);
	if (h_ret != 0) {
		oxr_handle_destroy(log, &act_set->handle);
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Failed to create loc_store hashset");
	}

	strncpy(act_set_ref->name, createInfo->actionSetName,
	        sizeof(act_set_ref->name));

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

static void
oxr_action_ref_destroy_cb(struct oxr_refcounted *orc)
{
	struct oxr_action_ref *act_ref = (struct oxr_action_ref *)orc;
	free(act_ref);
}

static XrResult
oxr_action_destroy_cb(struct oxr_logger *log, struct oxr_handle_base *hb)
{
	struct oxr_action *act = (struct oxr_action *)hb;

	oxr_refcounted_unref(&act->data->base);
	act->data = NULL;

	if (act->name_item != NULL) {
		u_hashset_erase_item(act->act_set->data->actions.name_store,
		                     act->name_item);
		free(act->name_item);
		act->name_item = NULL;
	}
	if (act->loc_item != NULL) {
		u_hashset_erase_item(act->act_set->data->actions.loc_store,
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

	if (!oxr_classify_sub_action_paths(
	        log, inst, createInfo->countSubactionPaths,
	        createInfo->subactionPaths, &sub_paths)) {
		return XR_ERROR_PATH_UNSUPPORTED;
	}

	struct oxr_action *act = NULL;
	OXR_ALLOCATE_HANDLE_OR_RETURN(log, act, OXR_XR_DEBUG_ACTION,
	                              oxr_action_destroy_cb, &act_set->handle);


	struct oxr_action_ref *act_ref = U_TYPED_CALLOC(struct oxr_action_ref);
	act_ref->base.destroy = oxr_action_ref_destroy_cb;
	oxr_refcounted_ref(&act_ref->base);
	act->data = act_ref;

	act_ref->act_key = key_gen++;
	act->act_key = act_ref->act_key;

	act->act_set = act_set;
	act_ref->sub_paths = sub_paths;
	act_ref->action_type = createInfo->actionType;

	strncpy(act_ref->name, createInfo->actionName, sizeof(act_ref->name));

	u_hashset_create_and_insert_str_c(act_set->data->actions.name_store,
	                                  createInfo->actionName,
	                                  &act->name_item);
	u_hashset_create_and_insert_str_c(act_set->data->actions.loc_store,
	                                  createInfo->localizedActionName,
	                                  &act->loc_item);

	*out_act = act;

	return XR_SUCCESS;
}


/*
 *
 * "Exported" helper functions.
 *
 */

bool
oxr_classify_sub_action_paths(struct oxr_logger *log,
                              struct oxr_instance *inst,
                              uint32_t num_subaction_paths,
                              const XrPath *subaction_paths,
                              struct oxr_sub_paths *sub_paths)
{
	const char *str = NULL;
	size_t length = 0;
	bool ret = true;

	// Reset the sub_paths completely.
	U_ZERO(sub_paths);

	if (num_subaction_paths == 0) {
		sub_paths->any = true;
		return ret;
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
		} else if (path == inst->path_cache.treadmill) {
			sub_paths->treadmill = true;
		} else {
			oxr_path_get_string(log, inst, path, &str, &length);

			oxr_warn(log, " unrecognized sub action path '%s'",
			         str);
			ret = false;
		}
	}
	return ret;
}

XrResult
oxr_action_get_pose_input(struct oxr_logger *log,
                          struct oxr_session *sess,
                          uint32_t act_key,
                          const struct oxr_sub_paths *sub_paths,
                          struct oxr_action_input **out_input)
{
	struct oxr_action_attachment *act_attached = NULL;

	oxr_session_get_action_attachment(sess, act_key, &act_attached);

	if (act_attached == NULL) {
		return XR_SUCCESS;
	}

	// Priority of inputs.
	if (act_attached->head.current.active &&
	    (sub_paths->head || sub_paths->any)) {
		*out_input = act_attached->head.inputs;
		return XR_SUCCESS;
	}
	if (act_attached->left.current.active &&
	    (sub_paths->left || sub_paths->any)) {
		*out_input = act_attached->left.inputs;
		return XR_SUCCESS;
	}
	if (act_attached->right.current.active &&
	    (sub_paths->right || sub_paths->any)) {
		*out_input = act_attached->right.inputs;
		return XR_SUCCESS;
	}
	if (act_attached->gamepad.current.active &&
	    (sub_paths->gamepad || sub_paths->any)) {
		*out_input = act_attached->gamepad.inputs;
		return XR_SUCCESS;
	}
	if (act_attached->user.current.active &&
	    (sub_paths->user || sub_paths->any)) {
		*out_input = act_attached->user.inputs;
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
          struct oxr_action_input inputs[16],
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
           struct oxr_action_output outputs[16],
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

/*!
 * Delegate to @ref do_outputs or @ref do_inputs depending on whether the action
 * is output or input.
 */
static bool
do_io_bindings(struct oxr_binding *b,
               struct oxr_action *act,
               struct xrt_device *xdev,
               struct oxr_action_input inputs[16],
               uint32_t *num_inputs,
               struct oxr_action_output outputs[16],
               uint32_t *num_outputs)
{
	bool found = false;

	if (act->data->action_type == XR_ACTION_TYPE_VIBRATION_OUTPUT) {
		found |= do_outputs(b, xdev, outputs, num_outputs);
	} else {
		found |= do_inputs(b, xdev, inputs, num_inputs);
	}

	return found;
}

static XrPath
get_matched_xrpath(struct oxr_binding *b, struct oxr_action *act)
{
	XrPath preferred_path = XR_NULL_PATH;
	for (uint32_t i = 0; i < b->num_keys; i++) {
		if (b->keys[i] == act->act_key) {
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
            struct oxr_action_input inputs[16],
            uint32_t *num_inputs,
            struct oxr_action_output outputs[16],
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
	oxr_binding_find_bindings_from_key(log, profile, act->act_key, bindings,
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



/*!
 * @public @memberof oxr_action_attachment
 */
static XrResult
oxr_action_attachment_bind(struct oxr_logger *log,
                           struct oxr_action_attachment *act_attached,
                           struct oxr_action *act,
                           struct oxr_interaction_profile *head,
                           struct oxr_interaction_profile *left,
                           struct oxr_interaction_profile *right,
                           struct oxr_interaction_profile *gamepad)
{
	struct oxr_sink_logger slog = {0};
	struct oxr_action_ref *act_ref = act->data;
	struct oxr_session *sess = act_attached->sess;

	// Start logging into a single buffer.
	oxr_slog(&slog, ": Binding %s/%s\n", act->act_set->data->name,
	         act_ref->name);

	if (act_ref->sub_paths.user || act_ref->sub_paths.any) {
#if 0
		oxr_action_bind_inputs(log, &slog, sess, act,
		                       &act_attached->user, user,
		                       OXR_SUB_ACTION_PATH_USER);
#endif
	}

	if (act_ref->sub_paths.head || act_ref->sub_paths.any) {
		oxr_action_bind_inputs(log, &slog, sess, act,
		                       &act_attached->head, head,
		                       OXR_SUB_ACTION_PATH_HEAD);
	}

	if (act_ref->sub_paths.left || act_ref->sub_paths.any) {
		oxr_action_bind_inputs(log, &slog, sess, act,
		                       &act_attached->left, left,
		                       OXR_SUB_ACTION_PATH_LEFT);
	}

	if (act_ref->sub_paths.right || act_ref->sub_paths.any) {
		oxr_action_bind_inputs(log, &slog, sess, act,
		                       &act_attached->right, right,
		                       OXR_SUB_ACTION_PATH_RIGHT);
	}

	if (act_ref->sub_paths.gamepad || act_ref->sub_paths.any) {
		oxr_action_bind_inputs(log, &slog, sess, act,
		                       &act_attached->gamepad, gamepad,
		                       OXR_SUB_ACTION_PATH_GAMEPAD);
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
oxr_action_cache_stop_output(struct oxr_logger *log,
                             struct oxr_session *sess,
                             struct oxr_action_cache *cache)
{
	// Set this as stopped.
	cache->stop_output_time = 0;

	union xrt_output_value value = {0};

	for (uint32_t i = 0; i < cache->num_outputs; i++) {
		struct oxr_action_output *output = &cache->outputs[i];
		struct xrt_device *xdev = output->xdev;

		xrt_device_set_output(xdev, output->name, &value);
	}
}

/*!
 * Called during xrSyncActions.
 *
 * @private @memberof oxr_action_cache
 */
static void
oxr_action_cache_update(struct oxr_logger *log,
                        struct oxr_session *sess,
                        struct oxr_action_cache *cache,
                        int64_t time,
                        bool selected)
{
	struct oxr_action_state last = cache->current;

	if (!selected) {
		if (cache->stop_output_time > 0) {
			oxr_action_cache_stop_output(log, sess, cache);
		}
		U_ZERO(&cache->current);
		return;
	}

	if (cache->num_outputs > 0) {
		cache->current.active = true;
		if (cache->stop_output_time < time) {
			oxr_action_cache_stop_output(log, sess, cache);
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
		 * @todo Combine multiple sources for a single subaction path.
		 */
		struct oxr_action_input *action_input = &(cache->inputs[0]);
		struct xrt_input *input = action_input->input;
		struct oxr_input_value_tagged raw_input = {
		    .type = XRT_GET_INPUT_TYPE(input->name),
		    .value = input->value,
		};
		struct oxr_input_value_tagged transformed = {0};
		if (!oxr_input_transform_process(action_input->transforms,
		                                 action_input->num_transforms,
		                                 &raw_input, &transformed)) {
			// We couldn't transform, how strange. Reset all state.
			// At this level we don't know what action this is, etc.
			// so a warning message isn't very helpful.
			U_ZERO(&cache->current);
			return;
		}
		int64_t timestamp = input->timestamp;
		bool changed = false;
		switch (transformed.type) {
		case XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE:
		case XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE: {
			changed =
			    (transformed.value.vec1.x != last.value.vec1.x);
			cache->current.value.vec1.x = transformed.value.vec1.x;
			break;
		}
		case XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE: {
			changed =
			    (transformed.value.vec2.x != last.value.vec2.x) ||
			    (transformed.value.vec2.y != last.value.vec2.y);
			cache->current.value.vec2.x = transformed.value.vec2.x;
			cache->current.value.vec2.y = transformed.value.vec2.y;
			break;
		}
#if 0
		case XRT_INPUT_TYPE_VEC3_MINUS_ONE_TO_ONE: {
			changed = (transformed.value.vec3.x != last.vec3.x) ||
			          (transformed.value.vec3.y != last.vec3.y) ||
			          (transformed.value.vec3.z != last.vec3.z);
			cache->current.vec3.x = transformed.value.vec3.x;
			cache->current.vec3.y = transformed.value.vec3.y;
			cache->current.vec3.z = transformed.value.vec3.z;
			break;
		}
#endif
		case XRT_INPUT_TYPE_BOOLEAN: {
			changed = (input->value.boolean != last.value.boolean);
			cache->current.value.boolean = input->value.boolean;
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
	if (act_attached->NAME.current.active) {                               \
		active |= true;                                                \
		value |= act_attached->NAME.current.value.boolean;             \
		timestamp = act_attached->NAME.current.timestamp;              \
	}
#define VEC1_CHECK(NAME)                                                       \
	if (act_attached->NAME.current.active) {                               \
		active |= true;                                                \
		if (value < act_attached->NAME.current.value.vec1.x) {         \
			value = act_attached->NAME.current.value.vec1.x;       \
			timestamp = act_attached->NAME.current.timestamp;      \
		}                                                              \
	}
#define VEC2_CHECK(NAME)                                                       \
	if (act_attached->NAME.current.active) {                               \
		active |= true;                                                \
		float curr_x = act_attached->NAME.current.value.vec2.x;        \
		float curr_y = act_attached->NAME.current.value.vec2.y;        \
		float curr_d = curr_x * curr_x + curr_y * curr_y;              \
		if (distance < curr_d) {                                       \
			x = curr_x;                                            \
			y = curr_y;                                            \
			distance = curr_d;                                     \
			timestamp = act_attached->NAME.current.timestamp;      \
		}                                                              \
	}

/*!
 * Called during each xrSyncActions.
 *
 * @private @memberof oxr_action_attachment
 */
static void
oxr_action_attachment_update(struct oxr_logger *log,
                             struct oxr_session *sess,
                             struct oxr_action_attachment *act_attached,
                             int64_t time,
                             struct oxr_sub_paths sub_paths)
{
	// This really shouldn't be happening.
	if (act_attached == NULL) {
		return;
	}

	//! @todo "/user" sub-action path.

	bool select_any = sub_paths.any;
	bool select_head = sub_paths.head || sub_paths.any;
	bool select_left = sub_paths.left || sub_paths.any;
	bool select_right = sub_paths.right || sub_paths.any;
	bool select_gamepad = sub_paths.gamepad || sub_paths.any;

	// clang-format off
	oxr_action_cache_update(log, sess, &act_attached->head, time, select_head);
	oxr_action_cache_update(log, sess, &act_attached->left, time, select_left);
	oxr_action_cache_update(log, sess, &act_attached->right, time, select_right);
	oxr_action_cache_update(log, sess, &act_attached->gamepad, time, select_gamepad);
	// clang-format on

	if (!select_any) {
		U_ZERO(&act_attached->any_state);
		return;
	}

	/*
	 * Any state.
	 */
	struct oxr_action_state last = act_attached->any_state;
	bool active = false;
	bool changed = false;
	XrTime timestamp = 0;

	switch (act_attached->act_ref->action_type) {
	case XR_ACTION_TYPE_BOOLEAN_INPUT: {
		bool value = false;
		BOOL_CHECK(user);
		BOOL_CHECK(head);
		BOOL_CHECK(left);
		BOOL_CHECK(right);
		BOOL_CHECK(gamepad);

		changed = (last.value.boolean != value);
		act_attached->any_state.value.boolean = value;
		break;
	}
	case XR_ACTION_TYPE_FLOAT_INPUT: {
		float value = -2.0;
		VEC1_CHECK(user);
		VEC1_CHECK(head);
		VEC1_CHECK(left);
		VEC1_CHECK(right);
		VEC1_CHECK(gamepad);

		changed = last.value.vec1.x != value;
		act_attached->any_state.value.vec1.x = value;
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

		changed = (last.value.vec2.x != x) || (last.value.vec2.y != y);
		act_attached->any_state.value.vec2.x = x;
		act_attached->any_state.value.vec2.y = y;
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
		U_ZERO(&act_attached->any_state);
	} else if (last.active && changed) {
		act_attached->any_state.timestamp = timestamp;
		act_attached->any_state.changed = true;
		act_attached->any_state.active = true;
	} else if (last.active) {
		act_attached->any_state.timestamp = last.timestamp;
		act_attached->any_state.changed = false;
		act_attached->any_state.active = true;
	} else {
		act_attached->any_state.timestamp = timestamp;
		act_attached->any_state.changed = false;
		act_attached->any_state.active = true;
	}
}
/*!
 * Try to produce a transform chain to convert the available input into the
 * desired input type.
 *
 * Populates @p action_input->transforms and @p action_input->num_transforms on
 * success.
 *
 * @returns false if it could not, true if it could
 */
static bool
oxr_action_populate_input_transform(struct oxr_logger *log,
                                    struct oxr_sink_logger *slog,
                                    struct oxr_session *sess,
                                    struct oxr_action *act,
                                    struct oxr_action_input *action_input,
                                    XrPath bound_path)
{
	assert(action_input->transforms == NULL);
	assert(action_input->num_transforms == 0);
	const char *str;
	size_t length;
	oxr_path_get_string(log, sess->sys->inst, bound_path, &str, &length);

	enum xrt_input_type t = XRT_GET_INPUT_TYPE(action_input->input->name);

	return oxr_input_transform_create_chain(
	    log, slog, t, act->data->action_type, act->data->name, str,
	    &action_input->transforms, &action_input->num_transforms);
}

static void
oxr_action_bind_inputs(struct oxr_logger *log,
                       struct oxr_sink_logger *slog,
                       struct oxr_session *sess,
                       struct oxr_action *act,
                       struct oxr_action_cache *cache,
                       struct oxr_interaction_profile *profile,
                       enum oxr_sub_action_path sub_path)
{
	struct oxr_action_input inputs[16] = {0};
	uint32_t num_inputs = 0;
	struct oxr_action_output outputs[16] = {0};
	uint32_t num_outputs = 0;

	//! @todo Should this be asserted to be non-null?
	XrPath bound_path = XR_NULL_PATH;
	get_binding(log, slog, sess, act, profile, sub_path, inputs,
	            &num_inputs, outputs, &num_outputs, &bound_path);

	cache->current.active = false;

	if (num_inputs > 0) {
		cache->current.active = true;
		cache->inputs =
		    U_TYPED_ARRAY_CALLOC(struct oxr_action_input, num_inputs);
		for (uint32_t i = 0; i < num_inputs; i++) {
			if (!oxr_action_populate_input_transform(
			        log, slog, sess, act, &(inputs[i]),
			        bound_path)) {
				/*!
				 * @todo de-populate this element if we couldn't
				 * get a transform?
				 */
				oxr_slog(
				    slog,
				    "Could not populate a transform for %s "
				    "despite it being bound!\n",
				    act->data->name);
			}
			cache->inputs[i] = inputs[i];
		}
		cache->num_inputs = num_inputs;
	}

	if (num_outputs > 0) {
		cache->current.active = true;
		cache->outputs =
		    U_TYPED_ARRAY_CALLOC(struct oxr_action_output, num_outputs);
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

/*!
 * Given an Action Set handle, return the @ref oxr_action_set and the associated
 * @ref oxr_action_set_attachment in the given Session.
 *
 * @private @memberof oxr_session
 */
static void
oxr_session_get_action_set_attachment(
    struct oxr_session *sess,
    XrActionSet actionSet,
    struct oxr_action_set_attachment **act_set_attached,
    struct oxr_action_set **act_set)
{
	void *ptr = NULL;
	*act_set =
	    XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_action_set *, actionSet);

	int ret = u_hashmap_int_find(sess->act_sets_attachments_by_key,
	                             (*act_set)->act_set_key, &ptr);
	if (ret == 0) {
		*act_set_attached = (struct oxr_action_set_attachment *)ptr;
	}
}

/*!
 * Given an action act_key, look up the @ref oxr_action_attachment of the
 * associated action in the given Session.
 *
 * @private @memberof oxr_session
 */
static void
oxr_session_get_action_attachment(
    struct oxr_session *sess,
    uint32_t act_key,
    struct oxr_action_attachment **out_act_attached)
{
	void *ptr = NULL;

	int ret =
	    u_hashmap_int_find(sess->act_attachments_by_key, act_key, &ptr);
	if (ret == 0) {
		*out_act_attached = (struct oxr_action_attachment *)ptr;
	}
}

static inline size_t
oxr_handle_base_get_num_children(struct oxr_handle_base *hb)
{
	size_t ret = 0;
	for (uint32_t i = 0; i < XRT_MAX_HANDLE_CHILDREN; ++i) {
		if (hb->children[i] != NULL) {
			++ret;
		}
	}
	return ret;
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

	oxr_find_profile_for_device(log, inst, sess->sys->head, &head);
	oxr_find_profile_for_device(log, inst, sess->sys->left, &left);
	oxr_find_profile_for_device(log, inst, sess->sys->right, &right);
	//! @todo add other subaction paths here

	// Before allocating, make sure nothing has been attached yet.

	for (uint32_t i = 0; i < bindInfo->countActionSets; i++) {
		struct oxr_action_set *act_set = XRT_CAST_OXR_HANDLE_TO_PTR(
		    struct oxr_action_set *, bindInfo->actionSets[i]);
		if (act_set->data->attached) {
			return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
		}
	}

	// Allocate room for list.
	sess->num_action_set_attachments = bindInfo->countActionSets;
	sess->act_set_attachments = U_TYPED_ARRAY_CALLOC(
	    struct oxr_action_set_attachment, sess->num_action_set_attachments);

	// Set up the per-session data for these action sets.
	for (uint32_t i = 0; i < sess->num_action_set_attachments; i++) {
		struct oxr_action_set *act_set = XRT_CAST_OXR_HANDLE_TO_PTR(
		    struct oxr_action_set *, bindInfo->actionSets[i]);
		struct oxr_action_set_ref *act_set_ref = act_set->data;
		act_set_ref->attached = true;
		struct oxr_action_set_attachment *act_set_attached =
		    &sess->act_set_attachments[i];
		oxr_action_set_attachment_init(log, sess, act_set,
		                               act_set_attached);

		// Allocate the action attachments for this set.
		act_set_attached->num_action_attachments =
		    oxr_handle_base_get_num_children(&act_set->handle);
		act_set_attached->act_attachments = U_TYPED_ARRAY_CALLOC(
		    struct oxr_action_attachment,
		    act_set_attached->num_action_attachments);

		// Set up the per-session data for the actions.
		uint32_t child_index = 0;
		for (uint32_t k = 0; k < XRT_MAX_HANDLE_CHILDREN; k++) {
			struct oxr_action *act =
			    (struct oxr_action *)act_set->handle.children[k];
			if (act == NULL) {
				continue;
			}

			struct oxr_action_attachment *act_attached =
			    &act_set_attached->act_attachments[child_index];
			oxr_action_attachment_init(log, act_set_attached,
			                           act_attached, act);
			oxr_action_attachment_bind(log, act_attached, act, head,
			                           left, right, NULL);
			++child_index;
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
	struct oxr_action_set_attachment *act_set_attached = NULL;

	// Check that all action sets has been attached.
	for (uint32_t i = 0; i < countActionSets; i++) {
		oxr_session_get_action_set_attachment(
		    sess, actionSets[i].actionSet, &act_set_attached, &act_set);
		if (act_set_attached == NULL) {
			return oxr_error(
			    log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
			    "(actionSets[%i].actionSet) action set '%s' has "
			    "not been attached to this session",
			    i, act_set != NULL ? act_set->data->name : "NULL");
		}
	}

	// Synchronize outputs to this time.
	int64_t now = time_state_get_now(sess->sys->inst->timekeeping);

	// Loop over all xdev devices.
	for (size_t i = 0; i < sess->sys->num_xdevs; i++) {
		oxr_xdev_update(sess->sys->xdevs[i]);
	}

	// Reset all action set attachments.
	for (size_t i = 0; i < sess->num_action_set_attachments; ++i) {
		act_set_attached = &sess->act_set_attachments[i];
		U_ZERO(&act_set_attached->requested_sub_paths);
	}

	// Go over all requested action sets and update their attachment.
	//! @todo can be listed more than once with different paths!
	for (uint32_t i = 0; i < countActionSets; i++) {
		struct oxr_sub_paths sub_paths;
		oxr_session_get_action_set_attachment(
		    sess, actionSets[i].actionSet, &act_set_attached, &act_set);
		assert(act_set_attached != NULL);

		if (!oxr_classify_sub_action_paths(log, sess->sys->inst, 1,
		                                   &actionSets[i].subactionPath,
		                                   &sub_paths)) {
			return XR_ERROR_PATH_UNSUPPORTED;
		}

		act_set_attached->requested_sub_paths.any |= sub_paths.any;
		act_set_attached->requested_sub_paths.user |= sub_paths.user;
		act_set_attached->requested_sub_paths.head |= sub_paths.head;
		act_set_attached->requested_sub_paths.left |= sub_paths.left;
		act_set_attached->requested_sub_paths.right |= sub_paths.right;
		act_set_attached->requested_sub_paths.gamepad |=
		    sub_paths.gamepad;
	}

	// Now, update all action attachments
	for (size_t i = 0; i < sess->num_action_set_attachments; ++i) {
		act_set_attached = &sess->act_set_attachments[i];
		struct oxr_sub_paths sub_paths =
		    act_set_attached->requested_sub_paths;


		for (uint32_t k = 0;
		     k < act_set_attached->num_action_attachments; k++) {
			struct oxr_action_attachment *act_attached =
			    &act_set_attached->act_attachments[k];

			if (act_attached == NULL) {
				continue;
			}

			oxr_action_attachment_update(log, sess, act_attached,
			                             now, sub_paths);
		}
	}


	return oxr_session_success_focused_result(sess);
}


/*
 *
 * Action get functions.
 *
 */

static void
get_state_from_state_bool(struct oxr_action_state *state,
                          XrActionStateBoolean *data)
{
	data->currentState = state->value.boolean;
	data->lastChangeTime = state->timestamp;
	data->changedSinceLastSync = state->changed;
	data->isActive = state->active;
	//! @todo
	// data->isActive = XR_TRUE;
}

static void
get_state_from_state_vec1(struct oxr_action_state *state,
                          XrActionStateFloat *data)
{
	data->currentState = state->value.vec1.x;
	data->lastChangeTime = state->timestamp;
	data->changedSinceLastSync = state->changed;
	data->isActive = state->active;
}

static void
get_state_from_state_vec2(struct oxr_action_state *state,
                          XrActionStateVector2f *data)
{
	data->currentState.x = state->value.vec2.x;
	data->currentState.y = state->value.vec2.y;
	data->lastChangeTime = state->timestamp;
	data->changedSinceLastSync = state->changed;
	data->isActive = XR_TRUE;
}

#define OXR_ACTION_GET_FILLER(TYPE)                                            \
	if (sub_paths.any && act_attached->any_state.active) {                 \
		get_state_from_state_##TYPE(&act_attached->any_state, data);   \
	}                                                                      \
	if (sub_paths.user && act_attached->user.current.active) {             \
		get_state_from_state_##TYPE(&act_attached->user.current,       \
		                            data);                             \
	}                                                                      \
	if (sub_paths.head && act_attached->head.current.active) {             \
		get_state_from_state_##TYPE(&act_attached->head.current,       \
		                            data);                             \
	}                                                                      \
	if (sub_paths.left && act_attached->left.current.active) {             \
		get_state_from_state_##TYPE(&act_attached->left.current,       \
		                            data);                             \
	}                                                                      \
	if (sub_paths.right && act_attached->right.current.active) {           \
		get_state_from_state_##TYPE(&act_attached->right.current,      \
		                            data);                             \
	}                                                                      \
	if (sub_paths.gamepad && act_attached->gamepad.current.active) {       \
		get_state_from_state_##TYPE(&act_attached->gamepad.current,    \
		                            data);                             \
	}


XrResult
oxr_action_get_boolean(struct oxr_logger *log,
                       struct oxr_session *sess,
                       uint32_t act_key,
                       struct oxr_sub_paths sub_paths,
                       XrActionStateBoolean *data)
{
	struct oxr_action_attachment *act_attached = NULL;

	oxr_session_get_action_attachment(sess, act_key, &act_attached);
	if (act_attached == NULL) {
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
                        uint32_t act_key,
                        struct oxr_sub_paths sub_paths,
                        XrActionStateFloat *data)
{
	struct oxr_action_attachment *act_attached = NULL;

	oxr_session_get_action_attachment(sess, act_key, &act_attached);
	if (act_attached == NULL) {
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
                        uint32_t act_key,
                        struct oxr_sub_paths sub_paths,
                        XrActionStateVector2f *data)
{
	struct oxr_action_attachment *act_attached = NULL;

	oxr_session_get_action_attachment(sess, act_key, &act_attached);
	if (act_attached == NULL) {
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
                    uint32_t act_key,
                    struct oxr_sub_paths sub_paths,
                    XrActionStatePose *data)
{
	struct oxr_action_attachment *act_attached = NULL;

	oxr_session_get_action_attachment(sess, act_key, &act_attached);
	if (act_attached == NULL) {
		return oxr_error(
		    log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		    "Action has not been attached to this session");
	}

	data->isActive = XR_FALSE;

	if (sub_paths.user || sub_paths.any) {
		data->isActive |= act_attached->user.current.active;
	}
	if (sub_paths.head || sub_paths.any) {
		data->isActive |= act_attached->head.current.active;
	}
	if (sub_paths.left || sub_paths.any) {
		data->isActive |= act_attached->left.current.active;
	}
	if (sub_paths.right || sub_paths.any) {
		data->isActive |= act_attached->right.current.active;
	}
	if (sub_paths.gamepad || sub_paths.any) {
		data->isActive |= act_attached->gamepad.current.active;
	}

	return oxr_session_success_result(sess);
}


/*
 *
 * Haptic feedback functions.
 *
 */

static void
set_action_output_vibration(struct oxr_session *sess,
                            struct oxr_action_cache *cache,
                            int64_t stop,
                            const XrHapticVibration *data)
{
	cache->stop_output_time = stop;

	union xrt_output_value value = {0};
	value.vibration.frequency = data->frequency;
	value.vibration.amplitude = data->amplitude;
	value.vibration.duration = data->duration;

	for (uint32_t i = 0; i < cache->num_outputs; i++) {
		struct oxr_action_output *output = &cache->outputs[i];
		struct xrt_device *xdev = output->xdev;

		xrt_device_set_output(xdev, output->name, &value);
	}
}



XrResult
oxr_action_apply_haptic_feedback(struct oxr_logger *log,
                                 struct oxr_session *sess,
                                 uint32_t act_key,
                                 struct oxr_sub_paths sub_paths,
                                 const XrHapticBaseHeader *hapticEvent)
{
	struct oxr_action_attachment *act_attached = NULL;

	oxr_session_get_action_attachment(sess, act_key, &act_attached);
	if (act_attached == NULL) {
		return oxr_error(
		    log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		    "Action has not been attached to this session");
	}

	const XrHapticVibration *data = (const XrHapticVibration *)hapticEvent;

	int64_t now = time_state_get_now(sess->sys->inst->timekeeping);
	int64_t stop = data->duration <= 0 ? now : now + data->duration;

	// clang-format off
	if (act_attached->user.current.active && (sub_paths.user || sub_paths.any)) {
		set_action_output_vibration(sess, &act_attached->user, stop, data);
	}
	if (act_attached->head.current.active && (sub_paths.head || sub_paths.any)) {
		set_action_output_vibration(sess, &act_attached->head, stop, data);
	}
	if (act_attached->left.current.active && (sub_paths.left || sub_paths.any)) {
		set_action_output_vibration(sess, &act_attached->left, stop, data);
	}
	if (act_attached->right.current.active && (sub_paths.right || sub_paths.any)) {
		set_action_output_vibration(sess, &act_attached->right, stop, data);
	}
	if (act_attached->gamepad.current.active && (sub_paths.gamepad || sub_paths.any)) {
		set_action_output_vibration(sess, &act_attached->gamepad, stop, data);
	}
	// clang-format on

	return oxr_session_success_result(sess);
}

XrResult
oxr_action_stop_haptic_feedback(struct oxr_logger *log,
                                struct oxr_session *sess,
                                uint32_t act_key,
                                struct oxr_sub_paths sub_paths)
{
	struct oxr_action_attachment *act_attached = NULL;

	oxr_session_get_action_attachment(sess, act_key, &act_attached);
	if (act_attached == NULL) {
		return oxr_error(
		    log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		    "Action has not been attached to this session");
	}

	// clang-format off
	if (act_attached->user.current.active && (sub_paths.user || sub_paths.any)) {
		oxr_action_cache_stop_output(log, sess, &act_attached->user);
	}
	if (act_attached->head.current.active && (sub_paths.head || sub_paths.any)) {
		oxr_action_cache_stop_output(log, sess, &act_attached->head);
	}
	if (act_attached->left.current.active && (sub_paths.left || sub_paths.any)) {
		oxr_action_cache_stop_output(log, sess, &act_attached->left);
	}
	if (act_attached->right.current.active && (sub_paths.right || sub_paths.any)) {
		oxr_action_cache_stop_output(log, sess, &act_attached->right);
	}
	if (act_attached->gamepad.current.active && (sub_paths.gamepad || sub_paths.any)) {
		oxr_action_cache_stop_output(log, sess, &act_attached->gamepad);
	}
	// clang-format on

	return oxr_session_success_result(sess);
}
