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
#include "bindings/b_generated_bindings.h"

#include "oxr_objects.h"
#include "oxr_logger.h"
#include "oxr_two_call.h"
#include "oxr_subaction.h"

#include <stdio.h>


static void
setup_paths(struct oxr_logger *log,
            struct oxr_instance *inst,
            const char **src_paths,
            XrPath **dest_paths,
            uint32_t *dest_path_count)
{
	uint32_t count = 0;
	while (src_paths[count] != NULL) {
		assert(count != UINT32_MAX);
		count++;
	}

	*dest_path_count = count;
	*dest_paths = U_TYPED_ARRAY_CALLOC(XrPath, count);

	for (size_t x = 0; x < *dest_path_count; x++) {
		const char *str = src_paths[x];
		size_t len = strlen(str);
		oxr_path_get_or_create(log, inst, str, len, &(*dest_paths)[x]);
	}
}

static bool
interaction_profile_find(struct oxr_logger *log,
                         struct oxr_instance *inst,
                         XrPath path,
                         struct oxr_interaction_profile **out_p)
{
	for (size_t x = 0; x < inst->profile_count; x++) {
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
get_subaction_path_from_path(struct oxr_logger *log,
                             struct oxr_instance *inst,
                             XrPath path,
                             enum oxr_subaction_path *out_subaction_path);

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

	for (size_t x = 0; x < NUM_PROFILE_TEMPLATES; x++) {
		XrPath t_path = XR_NULL_PATH;

		oxr_path_get_or_create(log, inst, profile_templates[x].path, strlen(profile_templates[x].path),
		                       &t_path);
		if (t_path == path) {
			templ = &profile_templates[x];
			break;
		}
	}

	if (templ == NULL) {
		*out_p = NULL;
		return false;
	}

	struct oxr_interaction_profile *p = U_TYPED_CALLOC(struct oxr_interaction_profile);

	p->xname = templ->name;
	p->binding_count = templ->binding_count;
	p->bindings = U_TYPED_ARRAY_CALLOC(struct oxr_binding, p->binding_count);
	p->dpad_count = templ->dpad_count;
	p->dpads = U_TYPED_ARRAY_CALLOC(struct oxr_dpad_emulation, p->dpad_count);
	p->path = path;
	p->localized_name = templ->localized_name;

	for (size_t x = 0; x < templ->binding_count; x++) {
		struct binding_template *t = &templ->bindings[x];
		struct oxr_binding *b = &p->bindings[x];

		XrPath subaction_path;
		XrResult r =
		    oxr_path_get_or_create(log, inst, t->subaction_path, strlen(t->subaction_path), &subaction_path);
		if (r != XR_SUCCESS) {
			oxr_log(log, "Couldn't get subaction path %s\n", t->subaction_path);
		}

		if (!get_subaction_path_from_path(log, inst, subaction_path, &b->subaction_path)) {
			oxr_log(log, "Invalid subaction path %s\n", t->subaction_path);
		}

		b->localized_name = t->localized_name;
		setup_paths(log, inst, t->paths, &b->paths, &b->path_count);
		b->input = t->input;
		b->dpad_activate = t->dpad_activate;
		b->output = t->output;
	}

	for (size_t x = 0; x < templ->dpad_count; x++) {
		struct dpad_emulation *t = &templ->dpads[x];
		struct oxr_dpad_emulation *d = &p->dpads[x];

		XrPath subaction_path;
		XrResult r =
		    oxr_path_get_or_create(log, inst, t->subaction_path, strlen(t->subaction_path), &subaction_path);
		if (r != XR_SUCCESS) {
			oxr_log(log, "Couldn't get subaction path %s\n", t->subaction_path);
		}

		if (!get_subaction_path_from_path(log, inst, subaction_path, &d->subaction_path)) {
			oxr_log(log, "Invalid subaction path %s\n", t->subaction_path);
		}

		setup_paths(log, inst, t->paths, &d->paths, &d->path_count);
		d->position = t->position;
		d->activate = t->activate;
	}

	// Add to the list of currently created interaction profiles.
	U_ARRAY_REALLOC_OR_FREE(inst->profiles, struct oxr_interaction_profile *, (inst->profile_count + 1));
	inst->profiles[inst->profile_count++] = p;

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
	binding->key_count = 0;
}

static void
reset_all_keys(struct oxr_binding *bindings, size_t binding_count)
{
	for (size_t x = 0; x < binding_count; x++) {
		reset_binding_keys(&bindings[x]);
	}
}

static void
add_key_to_matching_bindings(struct oxr_binding *bindings, size_t binding_count, XrPath path, uint32_t key)
{
	for (size_t x = 0; x < binding_count; x++) {
		struct oxr_binding *b = &bindings[x];

		bool found = false;
		uint32_t preferred_path_index;
		for (uint32_t y = 0; y < b->path_count; y++) {
			if (b->paths[y] == path) {
				found = true;
				preferred_path_index = y;
				break;
			}
		}

		if (!found) {
			continue;
		}

		U_ARRAY_REALLOC_OR_FREE(b->keys, uint32_t, (b->key_count + 1));
		U_ARRAY_REALLOC_OR_FREE(b->preferred_binding_path_index, uint32_t, (b->key_count + 1));
		b->preferred_binding_path_index[b->key_count] = preferred_path_index;
		b->keys[b->key_count++] = key;
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
get_subaction_path_from_path(struct oxr_logger *log,
                             struct oxr_instance *inst,
                             XrPath path,
                             enum oxr_subaction_path *out_subaction_path)
{
	const char *str = NULL;
	size_t length = 0;
	XrResult ret;

	ret = oxr_path_get_string(log, inst, path, &str, &length);
	if (ret != XR_SUCCESS) {
		return false;
	}

	if (length >= 10 && strncmp("/user/head", str, 10) == 0) {
		*out_subaction_path = OXR_SUB_ACTION_PATH_HEAD;
		return true;
	}
	if (length >= 15 && strncmp("/user/hand/left", str, 15) == 0) {
		*out_subaction_path = OXR_SUB_ACTION_PATH_LEFT;
		return true;
	}
	if (length >= 16 && strncmp("/user/hand/right", str, 16) == 0) {
		*out_subaction_path = OXR_SUB_ACTION_PATH_RIGHT;
		return true;
	}
	if (length >= 13 && strncmp("/user/gamepad", str, 13) == 0) {
		*out_subaction_path = OXR_SUB_ACTION_PATH_GAMEPAD;
		return true;
	}

	return false;
}

static const char *
get_subaction_path_str(enum oxr_subaction_path subaction_path)
{
	switch (subaction_path) {
	case OXR_SUB_ACTION_PATH_HEAD: return "Head";
	case OXR_SUB_ACTION_PATH_LEFT: return "Left";
	case OXR_SUB_ACTION_PATH_RIGHT: return "Right";
	case OXR_SUB_ACTION_PATH_GAMEPAD: return "Gameped";
	default: return NULL;
	}
}

static XrPath
get_interaction_bound_to_sub_path(struct oxr_session *sess, enum oxr_subaction_path subaction_path)
{
	switch (subaction_path) {
#define OXR_PATH_MEMBER(lower, CAP, _)                                                                                 \
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

	for (size_t i = 0; i < oip->binding_count; i++) {
		struct oxr_binding *binding = &oip->bindings[i];

		for (size_t k = 0; k < binding->path_count; k++) {
			if (binding->paths[k] != path) {
				continue;
			}
			str = binding->localized_name;
			i = oip->binding_count; // Break the outer loop as well.
			break;
		}
	}

	return str;
}

static void
get_profile_for_device_name(struct oxr_logger *log,
                            struct oxr_instance *inst,
                            enum xrt_device_name name,
                            struct oxr_interaction_profile **out_p)
{
	/*
	 * Map xrt_device_name to an interaction profile XrPath.
	 * Set *out_p to an oxr_interaction_profile if bindings for that interaction profile XrPath have been suggested.
	 */
#define FIND_PROFILE(PATH) interaction_profile_find(log, inst, inst->path_cache.PATH, out_p)

	switch (name) {
	case XRT_DEVICE_PSMV: FIND_PROFILE(mndx_ball_on_a_stick_controller); return;
	case XRT_DEVICE_SIMPLE_CONTROLLER: FIND_PROFILE(khr_simple_controller); return;
	case XRT_DEVICE_INDEX_CONTROLLER: FIND_PROFILE(valve_index_controller); return;
	case XRT_DEVICE_VIVE_WAND: FIND_PROFILE(htc_vive_controller); return;
	case XRT_DEVICE_TOUCH_CONTROLLER: FIND_PROFILE(oculus_touch_controller); return;
	case XRT_DEVICE_WMR_CONTROLLER: FIND_PROFILE(microsoft_motion_controller); return;
	case XRT_DEVICE_GO_CONTROLLER: FIND_PROFILE(oculus_go_controller); return;
	case XRT_DEVICE_VIVE_PRO: FIND_PROFILE(htc_vive_pro); return;
	case XRT_DEVICE_XBOX_CONTROLLER: FIND_PROFILE(microsoft_xbox_controller); return;
	case XRT_DEVICE_HAND_INTERACTION: FIND_PROFILE(msft_hand_interaction); return;

	// no interaction
	default:
	case XRT_DEVICE_HYDRA:
	case XRT_DEVICE_DAYDREAM:
	case XRT_DEVICE_GENERIC_HMD:
	case XRT_DEVICE_REALSENSE:
	case XRT_DEVICE_HAND_TRACKER:
	case XRT_DEVICE_VIVE_TRACKER_GEN1:
	case XRT_DEVICE_VIVE_TRACKER_GEN2: return;
	}

#undef FIND_PROFILE
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

	// Have bindings for this device's interaction profile been suggested?
	get_profile_for_device_name(log, inst, xdev->name, out_p);
	if (*out_p != NULL) {
		return;
	}

	// Check if bindings for any of this device's alternative interaction profiles have been suggested.
	for (size_t i = 0; i < xdev->binding_profile_count; i++) {
		struct xrt_binding_profile *xbp = &xdev->binding_profiles[i];
		get_profile_for_device_name(log, inst, xbp->name, out_p);
		if (*out_p != NULL) {
			return;
		}
	}
}

void
oxr_binding_find_bindings_from_key(struct oxr_logger *log,
                                   struct oxr_interaction_profile *p,
                                   uint32_t key,
                                   struct oxr_binding *bindings[OXR_MAX_BINDINGS_PER_ACTION],
                                   size_t *binding_count)
{
	if (p == NULL) {
		*binding_count = 0;
		return;
	}

	//! @todo This function should be a two call function, or handle more
	//! then 32 bindings.
	size_t num = 0;

	for (size_t y = 0; y < p->binding_count; y++) {
		struct oxr_binding *b = &p->bindings[y];

		for (size_t z = 0; z < b->key_count; z++) {
			if (b->keys[z] == key) {
				bindings[num++] = b;
				break;
			}
		}

		if (num >= 32) {
			*binding_count = num;
			return;
		}
	}

	*binding_count = num;
}

void
oxr_binding_destroy_all(struct oxr_logger *log, struct oxr_instance *inst)
{
	for (size_t x = 0; x < inst->profile_count; x++) {
		struct oxr_interaction_profile *p = inst->profiles[x];

		for (size_t y = 0; y < p->binding_count; y++) {
			struct oxr_binding *b = &p->bindings[y];

			reset_binding_keys(b);
			free(b->paths);
			b->paths = NULL;
			b->path_count = 0;
			b->input = 0;
			b->output = 0;
		}

		free(p->bindings);
		p->bindings = NULL;
		p->binding_count = 0;

		oxr_dpad_state_deinit(&p->dpad_state);

		free(p);
	}

	free(inst->profiles);
	inst->profiles = NULL;
	inst->profile_count = 0;
}


/*
 *
 * Client functions.
 *
 */

XrResult
oxr_action_suggest_interaction_profile_bindings(struct oxr_logger *log,
                                                struct oxr_instance *inst,
                                                const XrInteractionProfileSuggestedBinding *suggestedBindings,
                                                struct oxr_dpad_state *dpad_state)
{
	struct oxr_interaction_profile *p = NULL;

	// Path already validated.
	XrPath path = suggestedBindings->interactionProfile;
	interaction_profile_find_or_create(log, inst, path, &p);

	// Valid path, but not used.
	if (p == NULL) {
		goto out;
	}

	struct oxr_binding *bindings = p->bindings;
	size_t binding_count = p->binding_count;

	// Everything is now valid, reset the keys.
	reset_all_keys(bindings, binding_count);
	// Transfer ownership of dpad state to profile
	oxr_dpad_state_deinit(&p->dpad_state);
	p->dpad_state = *dpad_state;
	U_ZERO(dpad_state);

	for (size_t i = 0; i < suggestedBindings->countSuggestedBindings; i++) {
		const XrActionSuggestedBinding *s = &suggestedBindings->suggestedBindings[i];
		struct oxr_action *act = XRT_CAST_OXR_HANDLE_TO_PTR(struct oxr_action *, s->action);

		add_key_to_matching_bindings(bindings, binding_count, s->binding, act->act_key);
	}

out:
	oxr_dpad_state_deinit(dpad_state); // if it hasn't been moved

	return XR_SUCCESS;
}

XrResult
oxr_action_get_current_interaction_profile(struct oxr_logger *log,
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
#define IDENTIFY_TOP_LEVEL_PATH(X)                                                                                     \
	if (topLevelUserPath == inst->path_cache.X) {                                                                  \
		interactionProfile->interactionProfile = sess->X;                                                      \
	} else

	OXR_FOR_EACH_VALID_SUBACTION_PATH(IDENTIFY_TOP_LEVEL_PATH)
	{
		// else clause
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "Top level path not handled?!");
	}
#undef IDENTIFY_TOP_LEVEL_PATH
	return XR_SUCCESS;
}

XrResult
oxr_action_get_input_source_localized_name(struct oxr_logger *log,
                                           struct oxr_session *sess,
                                           const XrInputSourceLocalizedNameGetInfo *getInfo,
                                           uint32_t bufferCapacityInput,
                                           uint32_t *bufferCountOutput,
                                           char *buffer)
{
	char temp[1024] = {0};
	ssize_t current = 0;
	enum oxr_subaction_path subaction_path = 0;

	if (!get_subaction_path_from_path(log, sess->sys->inst, getInfo->sourcePath, &subaction_path)) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(getInfo->sourcePath) doesn't start with a "
		                 "valid subaction_path");
	}

	// Get the interaction profile bound to this subaction_path.
	XrPath path = get_interaction_bound_to_sub_path(sess, subaction_path);
	if (path == XR_NULL_PATH) {
		return oxr_error(log, XR_ERROR_VALIDATION_FAILURE,
		                 "(getInfo->sourcePath) no interaction profile "
		                 "bound to subaction path");
	}

	// Find the interaction profile.
	struct oxr_interaction_profile *oip = NULL;
	//! @todo: If we ever rebind a profile that has not been suggested by the client, it will not be found.
	interaction_profile_find(log, sess->sys->inst, path, &oip);
	if (oip == NULL) {
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, "no interaction profile found");
	}

	// Add which hand to use.
	if (getInfo->whichComponents & XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT) {
		add_string(temp, sizeof(temp), &current, get_subaction_path_str(subaction_path));
	}

	// Add a human readable and localized name of the device.
	if ((getInfo->whichComponents & XR_INPUT_SOURCE_LOCALIZED_NAME_INTERACTION_PROFILE_BIT) != 0) {
		add_string(temp, sizeof(temp), &current, oip->localized_name);
	}

	//! @todo This implementation is very very very inelegant.
	if ((getInfo->whichComponents & XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT) != 0) {
		/*
		 * The preceding enum is misnamed: it should be called identifier
		 * instead of component. But, this is a spec bug.
		 */
		add_string(temp, sizeof(temp), &current,
		           get_identifier_str_in_profile(log, sess->sys->inst, getInfo->sourcePath, oip));
	}

	// Include the null character.
	current += 1;

	OXR_TWO_CALL_HELPER(log, bufferCapacityInput, bufferCountOutput, buffer, (size_t)current, temp,
	                    oxr_session_success_result(sess));
}
