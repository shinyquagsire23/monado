// Copyright 2018-2019, Collabora, Ltd.
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

#include <stdio.h>


struct binding_template
{
	const char *paths[8];
	enum xrt_input_name inputs[8];
	enum xrt_output_name outputs[8];
	enum oxr_sub_action_path sub_path;
};

struct profile_template
{
	const char *path;



	struct binding_template *bindings;
	size_t num_bindings;
};

static struct binding_template khr_simple_controller_bindings[10];
static struct binding_template google_daydream_controller_bindings[12];

static struct profile_template profiles[2] = {
    {
        .path = "/interaction_profiles/khr/simple_controller",
        .bindings = khr_simple_controller_bindings,
        .num_bindings = ARRAY_SIZE(khr_simple_controller_bindings),
    },
    {
        .path = "/interaction_profiles/google/daydream_controller",
        .bindings = google_daydream_controller_bindings,
        .num_bindings = ARRAY_SIZE(google_daydream_controller_bindings),
    },
};

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
is_valid_interaction_profile(struct oxr_instance *inst, XrPath path)
{
	return inst->path_cache.khr_simple_controller == path ||
	       inst->path_cache.google_daydream_controller == path ||
	       inst->path_cache.htc_vive_controller == path ||
	       inst->path_cache.htc_vive_pro == path ||
	       inst->path_cache.microsoft_motion_controller == path ||
	       inst->path_cache.microsoft_xbox_controller == path ||
	       inst->path_cache.oculus_go_controller == path ||
	       inst->path_cache.oculus_touch_controller == path ||
	       inst->path_cache.valve_index_controller == path;
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
		} else {
			templ = NULL;
		}
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

	for (size_t x = 0; x < templ->num_bindings; x++) {
		struct binding_template *t = &templ->bindings[x];
		struct oxr_binding *b = &p->bindings[x];

		b->sub_path = t->sub_path;
		setup_paths(log, inst, t, b);
		setup_inputs(log, inst, t, b);
		setup_outputs(log, inst, t, b);
	}

	// Add to the list of currently created interaction profiles.
	size_t size =
	    sizeof(struct oxr_interaction_profile) * (inst->num_profiles + 1);
	inst->profiles = realloc(inst->profiles, size);
	inst->profiles[inst->num_profiles++] = p;

	*out_p = p;

	return true;
}

static void
reset_all_keys(struct oxr_binding *bindings, size_t num_bindings)
{
	for (size_t x = 0; x < num_bindings; x++) {
		free(bindings[x].keys);
		bindings[x].keys = NULL;
		bindings[x].num_keys = 0;
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
		for (size_t y = 0; y < b->num_paths; y++) {
			if (b->paths[y] == path) {
				found = true;
				break;
			}
		}

		if (!found) {
			continue;
		}

		size_t size = sizeof(uint32_t) * (b->num_keys + 1);
		b->keys = realloc(b->keys, size);
		b->keys[b->num_keys++] = key;
	}
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
	XrPath path = suggestedBindings->interactionProfile;
	const char *str;
	size_t length;

	// Check if this profile is valid.
	if (!is_valid_interaction_profile(inst, path)) {
		oxr_path_get_string(log, inst, path, &str, &length);

		return oxr_error(log, XR_ERROR_PATH_UNSUPPORTED,
		                 "(suggestedBindings->interactionProfile) "
		                 "non-supported profile '%s'",
		                 str);
	}

	interaction_profile_find_or_create(log, inst, path, &p);

	// Valid path, but not used.
	//! @todo Still needs to validate the paths.
	if (p == NULL) {
		return XR_SUCCESS;
	}

	struct oxr_binding *bindings = p->bindings;
	size_t num_bindings = p->num_bindings;

	//! @todo Validate keys **FIRST** then reset.
	reset_all_keys(bindings, num_bindings);

	for (size_t i = 0; i < suggestedBindings->countSuggestedBindings; i++) {
		const XrActionSuggestedBinding *s =
		    &suggestedBindings->suggestedBindings[i];
		struct oxr_action *act = (struct oxr_action *)s->action;

#if 0
		oxr_path_get_string(log, inst, s->binding, &str, &length);
		fprintf(stderr, "\t\t%s %i -> %s\n", act->name, act->key, str);
#endif

		add_key_to_matching_bindings(bindings, num_bindings, s->binding,
		                             act->key);
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

	if (!sess->actionsAttached) {
		return oxr_error(log, XR_ERROR_ACTIONSET_NOT_ATTACHED,
		                 " xrAttachSessionActionSets has not been "
		                 "called on this session.");
	}

	if (topLevelUserPath == inst->path_cache.head) {
		return sess->head;
	} else if (topLevelUserPath == inst->path_cache.left) {
		return sess->left;
	} else if (topLevelUserPath == inst->path_cache.right) {
		return sess->right;
	} else if (topLevelUserPath == inst->path_cache.gamepad) {
		return sess->gamepad;
	} else {
		return oxr_error(log, XR_ERROR_HANDLE_INVALID,
		                 " not implemented");
	}
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
	//! @todo Implement
	return oxr_error(log, XR_ERROR_HANDLE_INVALID, " not implemented");
}

XrResult
oxr_action_enumerate_bound_sources(struct oxr_logger *log,
                                   struct oxr_session *sess,
                                   uint64_t key,
                                   uint32_t sourceCapacityInput,
                                   uint32_t *sourceCountOutput,
                                   XrPath *sources)
{
	//! @todo Implement
	return oxr_error(log, XR_ERROR_HANDLE_INVALID, " not implemented");
}


/*
 *
 * Shipped bindings.
 *
 */

/*
 *
 *
 *
 * KHR Simple Controller
 *
 *
 *
 */

static struct binding_template khr_simple_controller_bindings[10] = {
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/select/click",
                "/user/hand/left/input/select",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                XRT_INPUT_HYDRA_TRIGGER_VALUE,
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/menu/click",
                "/user/hand/left/input/menu",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_MOVE_CLICK,
                XRT_INPUT_HYDRA_MIDDLE_CLICK,
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/grip/pose",
                "/user/hand/left/input/grip",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BODY_CENTER_POSE,
                XRT_INPUT_HYDRA_POSE,
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/aim/pose",
                "/user/hand/left/input/aim",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BALL_TIP_POSE,
                XRT_INPUT_HYDRA_POSE,
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/output/haptic",
                NULL,
            },
        .outputs =
            {
                XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION,
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/select/click",
                "/user/hand/right/input/select",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                XRT_INPUT_HYDRA_TRIGGER_VALUE,
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/menu/click",
                "/user/hand/right/input/menu",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_MOVE_CLICK,
                XRT_INPUT_HYDRA_MIDDLE_CLICK,
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/grip/pose",
                "/user/hand/right/input/grip",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BODY_CENTER_POSE,
                XRT_INPUT_HYDRA_POSE,
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/aim/pose",
                "/user/hand/right/input/aim",
                NULL,
            },
        .inputs =
            {
                XRT_INPUT_PSMV_BALL_TIP_POSE,
                XRT_INPUT_HYDRA_POSE,
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/output/haptic",
                NULL,
            },
        .outputs =
            {
                XRT_OUTPUT_NAME_PSMV_RUMBLE_VIBRATION,
                0,
            },
    },
};


/*
 *
 *
 *
 * Google Daydream Controller
 *
 *
 *
 */

static struct binding_template google_daydream_controller_bindings[12] = {
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/select/click",
                "/user/hand/left/input/select",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                XRT_INPUT_HYDRA_TRIGGER_VALUE,
#endif
                0,
            },
    },
    {
        //! @todo Flag that this is a trackpad
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/trackpad",
                "/user/hand/left/input/trackpad/x",
                "/user/hand/left/input/trackpad/y",
                NULL,
            },
        .inputs =
            {
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/trackpad/click",
                "/user/hand/left/input/trackpad",
                NULL,
            },
        .inputs =
            {
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/trackpad/touch",
                NULL,
            },
        .inputs =
            {
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/grip/pose",
                "/user/hand/left/input/grip",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_BODY_CENTER_POSE,
                XRT_INPUT_HYDRA_POSE,
#endif
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_LEFT,
        .paths =
            {
                "/user/hand/left/input/aim/pose",
                "/user/hand/left/input/aim",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_BALL_TIP_POSE,
                XRT_INPUT_HYDRA_POSE,
#endif
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/select/click",
                "/user/hand/right/input/select",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_TRIGGER_VALUE,
                XRT_INPUT_HYDRA_TRIGGER_VALUE,
#endif
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        //! @todo Flag that this is a trackpad
        .paths =
            {
                "/user/hand/right/input/trackpad",
                "/user/hand/right/input/trackpad/x",
                "/user/hand/right/input/trackpad/y",
                NULL,
            },
        .inputs =
            {
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/trackpad/click",
                "/user/hand/right/input/trackpad",
                NULL,
            },
        .inputs =
            {
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/trackpad/touch",
                NULL,
            },
        .inputs =
            {
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/grip/pose",
                "/user/hand/right/input/grip",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_BODY_CENTER_POSE,
                XRT_INPUT_HYDRA_POSE,
#endif
                0,
            },
    },
    {
        .sub_path = OXR_SUB_ACTION_PATH_RIGHT,
        .paths =
            {
                "/user/hand/right/input/aim/pose",
                "/user/hand/right/input/aim",
                NULL,
            },
        .inputs =
            {
#if 0
                XRT_INPUT_PSMV_BALL_TIP_POSE,
                XRT_INPUT_HYDRA_POSE,
#endif
                0,
            },
    },
};
