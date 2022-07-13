// Copyright 2018-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Handles transformation/filtering of input data.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_input_transform
 */

#include "math/m_mathinclude.h"

#include "oxr_input_transform.h"
#include "oxr_logger.h"
#include "oxr_objects.h"

#include "util/u_misc.h"

#include "openxr/openxr_reflection.h"

#include <string.h>
#include <assert.h>


static const char *
xr_action_type_to_str(XrActionType type)
{
	// clang-format off
	switch (type) {
#define PRINT(name, value) \
	case name: return #name;
	XR_LIST_ENUM_XrActionType(PRINT)
#undef PRINT
	default: return "XR_ACTION_TYPE_UNKNOWN";
	}
	// clang-format on
}

static const char *
xrt_input_type_to_str(enum xrt_input_type type)
{
	// clang-format off
	switch (type) {
	case XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE: return "XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE";
	case XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE: return "XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE";
	case XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE: return "XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE";
	case XRT_INPUT_TYPE_VEC3_MINUS_ONE_TO_ONE: return "XRT_INPUT_TYPE_VEC3_MINUS_ONE_TO_ONE";
	case XRT_INPUT_TYPE_BOOLEAN: return "XRT_INPUT_TYPE_BOOLEAN";
	case XRT_INPUT_TYPE_POSE: return "XRT_INPUT_TYPE_POSE";
	default: return "XRT_INPUT_UNKNOWN";
	}
	// clang-format on
}

/*!
 * Arbitrary but larger than required.
 */
#define OXR_MAX_INPUT_TRANSFORMS 5

void
oxr_input_transform_destroy(struct oxr_input_transform **transform_ptr)
{
	struct oxr_input_transform *xform = *transform_ptr;
	if (xform == NULL) {
		return;
	}
	free(xform);
	*transform_ptr = NULL;
}

bool
oxr_input_transform_init_root(struct oxr_input_transform *transform, enum xrt_input_type input_type)
{
	assert(transform != NULL);
	U_ZERO(transform);
	transform->type = INPUT_TRANSFORM_IDENTITY;
	transform->result_type = input_type;

	return true;
}

bool
oxr_input_transform_init_vec2_get_x(struct oxr_input_transform *transform, const struct oxr_input_transform *parent)
{
	assert(transform != NULL);
	assert(parent != NULL);
	assert(parent->result_type == XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE);

	U_ZERO(transform);
	transform->type = INPUT_TRANSFORM_VEC2_GET_X;
	transform->result_type = XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE;

	return true;
}

bool
oxr_input_transform_init_vec2_get_y(struct oxr_input_transform *transform, const struct oxr_input_transform *parent)
{
	assert(transform != NULL);
	assert(parent != NULL);
	assert(parent->result_type == XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE);

	U_ZERO(transform);
	transform->type = INPUT_TRANSFORM_VEC2_GET_Y;
	transform->result_type = XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE;

	return true;
}

bool
oxr_input_transform_init_vec2_dpad(struct oxr_input_transform *transform,
                                   const struct oxr_input_transform *parent,
                                   struct oxr_dpad_settings dpad_settings,
                                   enum oxr_dpad_region dpad_region,
                                   enum xrt_input_type activation_input_type,
                                   struct xrt_input *activation_input)
{
	assert(transform != NULL);
	assert(parent != NULL);
	assert(parent->result_type == XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE);

	U_ZERO(transform);
	transform->type = INPUT_TRANSFORM_DPAD;
	transform->result_type = XRT_INPUT_TYPE_BOOLEAN;
	transform->data.dpad_state.settings = dpad_settings;
	transform->data.dpad_state.bound_region = dpad_region;
	transform->data.dpad_state.activation_input_type = activation_input_type;
	transform->data.dpad_state.activation_input = activation_input;
	transform->data.dpad_state.already_active = activation_input == NULL;

	return true;
}

bool
oxr_input_transform_init_threshold(struct oxr_input_transform *transform,
                                   const struct oxr_input_transform *parent,
                                   float threshold,
                                   bool invert)
{
	assert(transform != NULL);
	assert(parent != NULL);
	assert((parent->result_type == XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE) ||
	       (parent->result_type == XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE));

	U_ZERO(transform);
	transform->type = INPUT_TRANSFORM_THRESHOLD;
	transform->result_type = XRT_INPUT_TYPE_BOOLEAN;
	transform->data.threshold.threshold = threshold;
	transform->data.threshold.invert = invert;

	return true;
}

bool
oxr_input_transform_init_bool_to_vec1(struct oxr_input_transform *transform,
                                      const struct oxr_input_transform *parent,
                                      enum xrt_input_type result_type,
                                      float true_val,
                                      float false_val)
{
	assert(transform != NULL);
	assert(parent != NULL);
	assert(parent->result_type == XRT_INPUT_TYPE_BOOLEAN);
	assert((result_type == XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE) ||
	       (result_type == XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE));

	U_ZERO(transform);
	transform->type = INPUT_TRANSFORM_BOOL_TO_VEC1;
	transform->result_type = result_type;
	transform->data.bool_to_vec1.true_val = true_val;
	transform->data.bool_to_vec1.false_val = false_val;

	return true;
}

bool
oxr_input_transform_process(struct oxr_input_transform *transform,
                            size_t transform_count,
                            const struct oxr_input_value_tagged *input,
                            struct oxr_input_value_tagged *out)
{
	if (transform == NULL) {
		return false;
	}
	struct oxr_input_value_tagged data = *input;
	for (size_t i = 0; i < transform_count; ++i) {
		struct oxr_input_transform *xform = &(transform[i]);
		switch (xform->type) {
		case INPUT_TRANSFORM_IDENTITY:
			// do nothing
			break;
		case INPUT_TRANSFORM_VEC2_GET_X: data.value.vec1.x = data.value.vec2.x; break;
		case INPUT_TRANSFORM_VEC2_GET_Y: data.value.vec1.x = data.value.vec2.y; break;
		case INPUT_TRANSFORM_THRESHOLD: {
			bool temp = data.value.vec1.x > xform->data.threshold.threshold;
			if (xform->data.threshold.invert) {
				temp = !temp;
			}
			data.value.boolean = temp;
			break;
		}
		case INPUT_TRANSFORM_BOOL_TO_VEC1: {
			data.value.vec1.x =
			    data.value.boolean ? xform->data.bool_to_vec1.true_val : xform->data.bool_to_vec1.false_val;
			break;
		}
		case INPUT_TRANSFORM_DPAD: {
			struct oxr_input_transform_dpad_data *dpad_state = &xform->data.dpad_state;

			if (dpad_state->activation_input != NULL) {
				bool active = true;

				switch (dpad_state->activation_input_type) {
				case XRT_INPUT_TYPE_BOOLEAN: {
					active = dpad_state->activation_input->value.boolean;
					break;
				}
				case XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE: {
					float force = dpad_state->activation_input->value.vec1.x;
					active = (force >= dpad_state->settings.forceThreshold) ||
					         (dpad_state->already_active &&
					          force >= dpad_state->settings.forceThresholdReleased);
					break;
				}
				default: active = false;
				}

				dpad_state->already_active = active;
				if (!active) {
					dpad_state->active_regions = OXR_DPAD_REGION_CENTER;
					data.value.boolean = false;
					break;
				}
			}

			enum oxr_dpad_region bound_region = dpad_state->bound_region;
			enum oxr_dpad_region active_regions = OXR_DPAD_REGION_CENTER;

			for (int i = 0; i < 4; i++) {
				enum oxr_dpad_region query_region = 1 << i;

				bool rot90 =
				    (query_region == OXR_DPAD_REGION_LEFT) || (query_region == OXR_DPAD_REGION_RIGHT);
				bool rot180 =
				    (query_region == OXR_DPAD_REGION_DOWN) || (query_region == OXR_DPAD_REGION_RIGHT);

				float localX = rot90 ? data.value.vec2.y : data.value.vec2.x;
				float localY = rot90 ? -data.value.vec2.x : data.value.vec2.y;
				if (rot180) {
					localX = -localX;
					localY = -localY;
				}

				float centerRadius = dpad_state->settings.centerRegion;
				if (localX * localX + localY * localY <= centerRadius * centerRadius) {
					continue;
				}

				float tanXY = atan2f(localX, localY);
				float halfAngle = dpad_state->settings.wedgeAngle / 2.0f;
				if (-halfAngle < tanXY && tanXY <= halfAngle) {
					active_regions |= query_region;
				}
			}

			if (!dpad_state->already_active || !dpad_state->settings.isSticky ||
			    (dpad_state->active_regions == OXR_DPAD_REGION_CENTER) ||
			    (active_regions == OXR_DPAD_REGION_CENTER)) {
				dpad_state->active_regions = active_regions;
			}

			data.value.boolean = (dpad_state->active_regions == bound_region) ||
			                     ((dpad_state->active_regions & bound_region) != 0);
			break;
		}
		case INPUT_TRANSFORM_INVALID:
		default: return false;
		}
		// Update the data type tag
		data.type = xform->result_type;
	}
	*out = data;
	return true;
}

static bool
ends_with(const char *str, const char *suffix)
{
	size_t len = strlen(str);
	size_t suffix_len = strlen(suffix);

	return (len >= suffix_len) && (0 == strcmp(str + (len - suffix_len), suffix));
}

static inline bool
input_is_float(enum xrt_input_type input_type)
{
	return (input_type == XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE) || (input_type == XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE);
}

static inline uint8_t
input_dim(enum xrt_input_type input_type)
{
	switch (input_type) {
	case XRT_INPUT_TYPE_BOOLEAN:
	case XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE:
	case XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE: return 1;
	case XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE: return 2;
	default: return 0;
	}
}

static inline bool
oxr_type_matches_xrt(enum xrt_input_type input_type, XrActionType result_type)
{
	switch (result_type) {
	case XR_ACTION_TYPE_BOOLEAN_INPUT: return input_type == XRT_INPUT_TYPE_BOOLEAN;
	case XR_ACTION_TYPE_FLOAT_INPUT: return input_is_float(input_type);
	case XR_ACTION_TYPE_VECTOR2F_INPUT: return input_type == XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE;
	default: return false;
	}
}

static inline bool
extend_transform_array(struct oxr_logger *log,
                       struct oxr_sink_logger *slog,
                       struct oxr_input_transform *transform,
                       const struct oxr_input_transform *parent,
                       XrActionType result_type,
                       const char *bound_path_string)
{
	enum xrt_input_type input_type = parent->result_type;
	if (input_dim(input_type) == 2 && result_type != XR_ACTION_TYPE_VECTOR2F_INPUT) {
		// reduce dimension
		if (ends_with(bound_path_string, "/x")) {
			oxr_slog(slog, "\t\t\tAdding transform: get x of Vec2\n");
			return oxr_input_transform_init_vec2_get_x(transform, parent);
		}
		if (ends_with(bound_path_string, "/y")) {
			oxr_slog(slog, "\t\t\tAdding transform: get y of Vec2\n");
			return oxr_input_transform_init_vec2_get_y(transform, parent);
		}
		oxr_slog(slog, "\t\t\tNo rule to get float from vec2f for binding %s\n", bound_path_string);
		return false;
	}

	if (input_type == XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE && result_type == XR_ACTION_TYPE_BOOLEAN_INPUT) {
		// 0.2 is for a little deadband around the center.
		oxr_slog(slog, "\t\t\tAdding transform: threshold [-1, 1] float\n");
		return oxr_input_transform_init_threshold(transform, parent, 0.2f, false);
	}

	if (input_type == XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE && result_type == XR_ACTION_TYPE_BOOLEAN_INPUT) {
		// Need it pressed nearly all the way
		oxr_slog(slog, "\t\t\tAdding transform: threshold [0, 1] float\n");
		return oxr_input_transform_init_threshold(transform, parent, 0.7f, false);
	}

	if (input_type == XRT_INPUT_TYPE_BOOLEAN && result_type == XR_ACTION_TYPE_FLOAT_INPUT) {
		// this conversion is in the spec
		oxr_slog(slog, "\t\t\tAdding transform: bool to float\n");
		return oxr_input_transform_init_bool_to_vec1(transform, parent, XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE, 1.f,
		                                             0.f);
	}

	oxr_slog(slog, "\t\t\tCould not transform!\n");

	return false;
}

struct oxr_input_transform *
oxr_input_transform_clone_chain(struct oxr_input_transform *transforms, size_t transform_count)
{
	struct oxr_input_transform *ret = U_TYPED_ARRAY_CALLOC(struct oxr_input_transform, transform_count);
	memcpy(ret, transforms, sizeof(*ret) * transform_count);
	return ret;
}

bool
oxr_input_transform_create_chain(struct oxr_logger *log,
                                 struct oxr_sink_logger *slog,
                                 enum xrt_input_type input_type,
                                 XrActionType result_type,
                                 const char *action_name,
                                 const char *bound_path_string,
                                 struct oxr_input_transform **out_transforms,
                                 size_t *out_transform_count)
{
	struct oxr_input_transform chain[OXR_MAX_INPUT_TRANSFORMS] = {0};

	oxr_slog(slog, "\t\tAdding transform from '%s' to '%s'\n", xr_action_type_to_str(result_type),
	         xrt_input_type_to_str(input_type));

	struct oxr_input_transform *current_xform = &(chain[0]);
	if (!oxr_input_transform_init_root(current_xform, input_type)) {
		*out_transform_count = 0;
		*out_transforms = NULL;
		return false;
	}

	bool identity = (result_type == XR_ACTION_TYPE_POSE_INPUT && input_type == XRT_INPUT_TYPE_POSE) ||
	                oxr_type_matches_xrt(current_xform->result_type, result_type);

	if (identity) {
		// No transform needed, just return identity to keep this alive.
		*out_transform_count = 1;
		*out_transforms = oxr_input_transform_clone_chain(chain, 1);
		oxr_slog(slog, "\t\t\tUsing identity transform for input.\n");
		return true;
	}

	// We start over here.
	size_t transform_count = 0;
	while (!oxr_type_matches_xrt(current_xform->result_type, result_type)) {
		if (transform_count >= OXR_MAX_INPUT_TRANSFORMS) {
			// Couldn't finish the transform to the desired type.
			oxr_slog(
			    slog,
			    "\t\t\tSeem to have gotten into a loop, trying to make a rule to transform. '%s' '%s' \n",
			    action_name, bound_path_string);
			*out_transform_count = 0;
			*out_transforms = NULL;
			return false;
		}

		struct oxr_input_transform *new_xform = &(chain[transform_count]);
		if (!extend_transform_array(log, slog, new_xform, current_xform, result_type, bound_path_string)) {
			// Error has already been logged.

			*out_transform_count = 0;
			*out_transforms = NULL;
			return false;
		}

		transform_count++;
		current_xform = new_xform;
	}

	*out_transform_count = transform_count;
	*out_transforms = oxr_input_transform_clone_chain(chain, transform_count);

	return true;
}

bool
oxr_input_transform_create_chain_dpad(struct oxr_logger *log,
                                      struct oxr_sink_logger *slog,
                                      enum xrt_input_type input_type,
                                      XrActionType result_type,
                                      const char *bound_path_string,
                                      struct oxr_dpad_binding_modification *dpad_binding_modification,
                                      enum oxr_dpad_region dpad_region,
                                      enum xrt_input_type activation_input_type,
                                      struct xrt_input *activation_input,
                                      struct oxr_input_transform **out_transforms,
                                      size_t *out_transform_count)
{
	struct oxr_input_transform chain[OXR_MAX_INPUT_TRANSFORMS] = {0};

	// these default settings are specified by OpenXR and thus must not be changed
	struct oxr_dpad_settings dpad_settings = {
	    .forceThreshold = 0.5f,
	    .forceThresholdReleased = 0.4f,
	    .centerRegion = 0.5f,
	    .wedgeAngle = (float)M_PI_2,
	    .isSticky = false,
	};

	if (dpad_binding_modification != NULL) {
		dpad_settings = dpad_binding_modification->settings;
	}

	oxr_slog(slog, "\t\tAdding dpad transform from '%s' to '%s'\n", xr_action_type_to_str(result_type),
	         xrt_input_type_to_str(input_type));

	struct oxr_input_transform *current_xform = &(chain[0]);
	if (!oxr_input_transform_init_root(current_xform, input_type)) {
		*out_transform_count = 0;
		*out_transforms = NULL;
		return false;
	}

	// We start over here.
	size_t transform_count = 0;
	input_type = current_xform->result_type;
	if (input_type != XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE) {
		oxr_slog(slog, "\t\t\tUnexpected input type for dpad binding %s\n", bound_path_string);
		return false;
	}
	if (result_type != XR_ACTION_TYPE_BOOLEAN_INPUT) {
		oxr_slog(slog, "\t\t\tUnexpected output type for dpad binding %s\n", bound_path_string);
		return false;
	}

	struct oxr_input_transform *new_xform = &(chain[transform_count]);
	if (!oxr_input_transform_init_vec2_dpad(new_xform, current_xform, dpad_settings, dpad_region,
	                                        activation_input_type, activation_input)) {
		// Error has already been logged.

		*out_transform_count = 0;
		*out_transforms = NULL;
		return false;
	}

	current_xform = new_xform;
	transform_count++;

	*out_transform_count = transform_count;
	*out_transforms = oxr_input_transform_clone_chain(chain, transform_count);

	return true;
}
