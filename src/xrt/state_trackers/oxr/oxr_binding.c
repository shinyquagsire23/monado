// Copyright 2018-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds binding related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "util/u_misc.h"

#include "xrt/xrt_compiler.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_subaction.h"
#include "oxr_binding_data.h"

#include <stdio.h>


static void
setup_paths(struct oxr_logger *log,
            struct oxr_instance *inst,
            struct binding_template *templ,
            struct oxr_binding *binding)
{
	size_t count = 0;
	while (templ->paths[count] != NULL) {
		count++;
	}

	binding->num_paths = count;
	binding->paths = U_TYPED_ARRAY_CALLOC(XrPath, count);

	for (size_t x = 0; x < binding->num_paths; x++) {
		const char *str = templ->paths[x];
		size_t len = strlen(str);
		oxr_path_get_or_create(log, inst, str, len, &binding->paths[x]);
	}
}

static void
setup_inputs(struct oxr_logger *log,
             struct oxr_instance *inst,
             struct binding_template *templ,
             struct oxr_binding *binding)
{
	size_t count = 0;
	while (templ->inputs[count] != 0) {
		count++;
	}

	if (count == 0) {
		return;
	}

	binding->num_inputs = count;
	binding->inputs = U_TYPED_ARRAY_CALLOC(enum xrt_input_name, count);

	for (size_t x = 0; x < binding->num_inputs; x++) {
		binding->inputs[x] = templ->inputs[x];
	}
}

static void
setup_outputs(struct oxr_logger *log,
              struct oxr_instance *inst,
              struct binding_template *templ,
              struct oxr_binding *binding)
{
	size_t count = 0;
	while (templ->outputs[count] != 0) {
		count++;
	}

	if (count == 0) {
		return;
	}

	binding->num_outputs = count;
	binding->outputs = U_TYPED_ARRAY_CALLOC(enum xrt_output_name, count);

	for (size_t x = 0; x < binding->num_outputs; x++) {
		binding->outputs[x] = templ->outputs[x];
	}
}

static bool
interaction_profile_find(struct oxr_logger *log,
                         struct oxr_instance *inst,
                         XrPath path,
                         struct oxr_interaction_profile **out_p)
{
	for (size_t x = 0; x < inst->num_profiles; x++) {
		struct oxr_interaction_profile *p = inst->profiles[x];
		if (p->path != path) {
			continue;
		}

		*out_p = p;
		return true;
	}

	return false;
}

static bool
interaction_profile_find_or_create(struct oxr_logger *log,
                                   struct oxr_instance *inst,
                                   XrPath path,
                                   struct oxr_interaction_profile **out_p)
{
	if (interaction_profile_find(log, inst, path, out_p)) {
		return true;
	}

	struct profile_template *templ = NULL;
	for (size_t x = 0; x < ARRAY_SIZE(profiles); x++) {
		templ = &profiles[x];
		XrPath t_path = XR_NULL_PATH;

		oxr_path_get_or_create(log, inst, templ->path,
		                       strlen(templ->path), &t_path);
		if (t_path == path) {
			break;
		}
		templ = NULL;
	}

	if (templ == NULL) {
		*out_p = NULL;
		return false;
	}

	struct oxr_interaction_profile *p =
	    U_TYPED_CALLOC(struct oxr_interaction_profile);

	p->num_bindings = templ->num_bindings;
	p->bindings = U_TYPED_ARRAY_CALLOC(struct oxr_binding, p->num_bindings);
	p->path = path;
	p->localized_name = templ->localized_name;

	for (size_t x = 0; x < templ->num_bindings; x++) {
		struct binding_template *t = &templ->bindings[x];
		struct oxr_binding *b = &p->bindings[x];

		b->sub_path = t->sub_path;
		b->localized_name = t->localized_name;
		setup_paths(log, inst, t, b);
		setup_inputs(log, inst, t, b);
		setup_outputs(log, inst, t, b);
	}

	// Add to the list of currently created interaction profiles.
	U_ARRAY_REALLOC_OR_FREE(inst->profiles,
	                        struct oxr_interaction_profile *,
	                        (inst->num_profiles + 1));
	inst->profiles[inst->num_profiles++] = p;

	*out_p = p;

	return true;
}

static void
reset_binding_keys(struct oxr_binding *binding)
{
	free(binding->keys);
	free(binding->preferred_binding_path_index);
	binding->keys = NULL;
	binding->preferred_binding_path_index = NULL;
	binding->num_keys = 0;
}

static void
reset_all_keys(struct oxr_binding *bindings, size_t num_bindings)
{
	for (size_t x = 0; x < num_bindings; x++) {
		reset_binding_keys(&bindings[x]);
	}
}

static void
add_key_to_matching_bindings(struct oxr_binding *bindings,
                             size_t num_bindings,
                             XrPath path,
                             uint32_t key)
{
	for (size_t x = 0; x < num_bindings; x++) {
		struct oxr_binding *b = &bindings[x];

		bool found = false;
		uint32_t preferred_path_index;
		for (size_t y = 0; y < b->num_paths; y++) {
			if (b->paths[y] == path) {
				found = true;
				preferred_path_index = y;
				break;
			}
		}

		if (!found) {
			continue;
		}

		U_ARRAY_REALLOC_OR_FREE(b->keys, uint32_t, (b->num_keys + 1));
		U_ARRAY_REALLOC_OR_FREE(b->preferred_binding_path_index,
		                        uint32_t, (b->num_keys + 1));
		b->preferred_binding_path_index[b->num_keys] =
		    preferred_path_index;
		b->keys[b->num_keys++] = key;
	}
}

static void
add_string(char *temp, size_t max, ssize_t *current, const char *str)
{
	if (*current > 0) {
		temp[(*current)++] = ' ';
	}

	ssize_t len = snprintf(temp + *current, max - *current, "%s", str);
	if (len > 0) {
		*current += len;
	}
}

static bool
get_sub_path_from_path(struct oxr_logger *log,
                       struct oxr_instance *inst,
                       XrPath path,
                       enum oxr_sub_action_path *out_sub_path)
{
	const char *str = NULL;
	size_t length = 0;
	XrResult ret;

	ret = oxr_path_get_string(log, inst, path, &str, &length);
	if (ret != XR_SUCCESS) {
		return false;
	}

	if (length >= 10 && strncmp("/user/head", str, 10) == 0) {
		*out_sub_path = OXR_SUB_ACTION_PATH_HEAD;
		return true;
	}
	if (length >= 15 && strncmp("/user/hand/left", str, 15) == 0) {
		*out_sub_path = OXR_SUB_ACTION_PATH_LEFT;
		return true;
	}
	if (length >= 16 && strncmp("/user/hand/right", str, 16) == 0) {
		*out_sub_path = OXR_SUB_ACTION_PATH_RIGHT;
		return true;
	}
	if (length >= 13 && strncmp("/user/gamepad", str, 13) == 0) {
		*out_sub_path = OXR_SUB_ACTION_PATH_GAMEPAD;
		return true;
	}

	return false;
}

static const char *
get_sub_path_str(enum oxr_sub_action_path sub_path)
{
	switch (sub_path) {
	case OXR_SUB_ACTION_PATH_HEAD: return "Head";
	case OXR_SUB_ACTION_PATH_LEFT: return "Left";
	case OXR_SUB_ACTION_PATH_RIGHT: return "Right";
	case OXR_SUB_ACTION_PATH_GAMEPAD: return "Gameped";
	default: return NULL;
	}
}

static XrPath
get_interaction_bound_to_sub_path(struct oxr_session *sess,
                                  enum oxr_sub_action_path sub_path)
{
	switch (sub_path) {
#define OXR_PATH_MEMBER(lower, CAP, _)                                         \
	case OXR_SUB_ACTION_PATH_##CAP: return sess->lower;

		OXR_FOR_EACH_VALID_SUBACTION_PATH_DETAILED(OXR_PATH_MEMBER)
#undef OXR_PATH_MEMBER
	default: return XR_NULL_PATH;
	}
}

static const char *
get_identifier_str_in_profile(struct oxr_logger *log,
                              struct oxr_instance *inst,
                              XrPath path,
                              struct oxr_interaction_profile *oip)
{
	const char *str = NULL;
	size_t length = 0;
	XrResult ret;

	ret = oxr_path_get_string(log, inst, path, &str, &length);
	if (ret != XR_SUCCESS) {
		return NULL;
	}

	for (size_t i = 0; i < oip->num_bindings; i++) {
		struct oxr_binding *binding = &oip->bindings[i];

		for (size_t k = 0; k < binding->num_paths; k++) {
			if (binding->paths[k] != path) {
				continue;
			}
			str = binding->localized_name;
			i = oip->num_bindings; // Break the outer loop as well.
			break;
		}
	}

	return str;
}


/*
 *
 * 'Exported' functions.
 *
 */

void
oxr_find_profile_for_device(struct oxr_logger *log,
                            struct oxr_instance *inst,
                            struct xrt_device *xdev,
                            struct oxr_interaction_profile **out_p)
{
	if (xdev == NULL) {
		return;
	}

	enum xrt_device_name name = xdev->name;

	//! @todo A lot more clever selecting the profile here.
	switch (name) {
	case XRT_DEVICE_HYDRA:
		// clang-format off
		interaction_profile_find(log, inst, inst->path_cache.khr_simple_controller, out_p);
		// clang-format on
		return;
	case XRT_DEVICE_PSMV:
		// clang-format off
		interaction_profile_find(log, inst, inst->path_cache.khr_simple_controller, out_p);
		interaction_profile_find(log, inst, inst->path_cache.mndx_ball_on_a_stick_controller, out_p);
		// clang-format on
		return;
	case XRT_DEVICE_DAYDREAM:
		interaction_profile_find(
		    log, inst, inst->path_cache.khr_simple_controller, out_p);
		return;
	case XRT_DEVICE_INDEX_CONTROLLER:
		// clang-format off
		interaction_profile_find(log, inst, inst->path_cache.khr_simple_controller, out_p);
		interaction_profile_find(log, inst, inst->path_cache.valve_index_controller, out_p);
		// clang-format on
		return;
	case XRT_DEVICE_VIVE_WAND:
		// clang-format off
		interaction_profile_find(log, inst, inst->path_cache.khr_simple_controller, out_p);
		interaction_profile_find(log, inst, inst->path_cache.htc_vive_controller, out_p);
		// clang-format on
		return;
	default: return;
	}
}

void
oxr_binding_find_bindings_from_key(struct oxr_logger *log,
                                   struct oxr_interaction_profile *p,
                                   uint32_t key,
                                   struct oxr_binding *bindings[32],
                                   size_t *num_bindings)
{
	if (p == NULL) {
		*num_bindings = 0;
		return;
	}

	//! @todo This function should be a two call function, or handle more
	//! then 32 bindings.
	size_t num = 0;

	for (size_t y = 0; y < p->num_bindings; y++) {
		struct oxr_binding *b = &p->bindings[y];

		for (size_t z = 0; z < b->num_keys; z++) {
			if (b->keys[z] == key) {
				bindings[num++] = b;
				break;
			}
		}

		if (num >= 32) {
			*num_bindings = num;
			return;
		}
	}

	*num_bindings = num;
}

void
oxr_binding_destroy_all(struct oxr_logger *log, struct oxr_instance *inst)
{
	for (size_t x = 0; x < inst->num_profiles; x++) {
		struct oxr_interaction_profile *p = inst->profiles[x];

		for (size_t y = 0; y < p->num_bindings; y++) {
			struct oxr_binding *b = &p->bindings[y];

			reset_binding_keys(b);
			free(b->paths);
			free(b->inputs);
			free(b->outputs);

			b->paths = NULL;
			b->inputs = NULL;
			b->outputs = NULL;
			b->num_paths = 0;
			b->num_inputs = 0;
			b->num_outputs = 0;
		}

		free(p->bindings);
		p->bindings = NULL;
		p->num_bindings = 0;

		free(p);
	}

	free(inst->profiles);
	inst->profiles = NULL;
	inst->num_profiles = 0;
}


/*
 *
 * Client functions.
 *
 */

XrResult
oxr_action_suggest_interaction_profile_bindings(
    struct oxr_logger *log,
    struct oxr_instance *inst,
    const XrInteractionProfileSuggestedBinding *suggestedBindings)
{
	struct oxr_interaction_profile *p = NULL;

	// Path already validated.
	XrPath path = suggestedBindings->interactionProfile;
	interaction_profile_find_or_create(log, inst, path, &p);

	// Valid path, but not used.
	if (p == NULL) {
		return XR_SUCCESS;
	}

	struct oxr_binding *bindings = p->bindings;
	size_t num_bindings = p->num_bindings;

	// Everything is now valid, reset the keys.
	reset_all_keys(bindings, num_bindings);

	for (size_t i = 0; i < suggestedBindings->countSuggestedBindings; i++) {
		const XrActionSuggestedBinding *s =
		    &suggestedBindings->suggestedBindings[i];
		struct oxr_action *act =
		    XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_action *, s->action);

		add_key_to_matching_bindings(bindings, num_bindings, s->binding,
		                             act->act_key);
	}

	return XR_SUCCESS;
}

XrResult
oxr_action_get_current_interaction_profile(
    struct oxr_logger *log,
    struct oxr_session *sess,
    XrPath topLevelUserPath,
    XrInteractionProfileState *interactionProfile)
{
	struct oxr_instance *inst = sess->sys->inst;

	if (sess->act_set_attachments == NULL) {
		return oxr_error(log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		                 "xrAttachSessionActionSets has not been "
		                 "called on this session.");
	}
#define IDENTIFY_TOP_LEVEL_PATH(X)                                             \
	if (topLevelUserPath == inst->path_cache.X) {                          \
		interactionProfile->interactionProfile = sess->X;              \
	} else

	OXR_FOR_EACH_VALID_SUBACTION_PATH(IDENTIFY_TOP_LEVEL_PATH)
	{
		// else clause
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "Top level path not handled?!");
	}
#undef IDENTIFY_TOP_LEVEL_PATH
	return XR_SUCCESS;
}

XrResult
oxr_action_get_input_source_localized_name(
    struct oxr_logger *log,
    struct oxr_session *sess,
    const XrInputSourceLocalizedNameGetInfo *getInfo,
    uint32_t bufferCapacityInput,
    uint32_t *bufferCountOutput,
    char *buffer)
{
	char temp[1024] = {0};
	ssize_t current = 0;
	enum oxr_sub_action_path sub_path = 0;

	if (!get_sub_path_from_path(log, sess->sys->inst, getInfo->sourcePath,
	                            &sub_path)) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(getInfo->sourcePath) doesn't start with a "
		                 "valid sub_path");
	}

	// Get the interaction profile bound to this sub_path.
	XrPath path = get_interaction_bound_to_sub_path(sess, sub_path);
	if (path == XR_NULL_PATH) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(getInfo->sourcePath) no interaction profile "
		                 "bound to sub path");
	}

	// Find the interaction profile.
	struct oxr_interaction_profile *oip = NULL;
	interaction_profile_find_or_create(log, sess->sys->inst, path, &oip);
	if (oip == NULL) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
		                 "no interaction profile found");
	}

	// Add which hand to use.
	if (getInfo->whichComponents &
	    XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT) {
		add_string(temp, sizeof(temp), &current,
		           get_sub_path_str(sub_path));
	}

	// Add a human readable and localized name of the device.
	if ((getInfo->whichComponents &
	     XR_INPUT_SOURCE_LOCALIZED_NAME_INTERACTION_PROFILE_BIT) != 0) {
		add_string(temp, sizeof(temp), &current, oip->localized_name);
	}

	//! @todo This implementation is very very very ugly.
	if ((getInfo->whichComponents &
	     XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT) != 0) {
		/*
		 * The above enum is miss-named it should be called identifier
		 * instead of component.
		 */
		add_string(temp, sizeof(temp), &current,
		           get_identifier_str_in_profile(
		               log, sess->sys->inst, getInfo->sourcePath, oip));
	}

	OXR_TWO_CALL_HELPER(log, bufferCapacityInput, bufferCountOutput, buffer,
	                    (size_t)current, temp,
	                    oxr_session_success_result(sess));
}
