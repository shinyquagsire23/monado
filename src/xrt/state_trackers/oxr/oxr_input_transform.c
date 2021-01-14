// Copyright 2018-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Handles transformation/filtering of input data.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_input_transform
 */

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
oxr_input_transform_process(const struct oxr_input_transform *transform,
                            size_t num_transforms,
                            const struct oxr_input_value_tagged *input,
                            struct oxr_input_value_tagged *out)
{
	if (transform == NULL) {
		return false;
	}
	struct oxr_input_value_tagged data = *input;
	for (size_t i = 0; i < num_transforms; ++i) {
		const struct oxr_input_transform *xform = &(transform[i]);
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
	int len = strlen(str);
	int suffix_len = strlen(suffix);

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
oxr_input_transform_clone_chain(struct oxr_input_transform *transforms, size_t num_transforms)
{
	struct oxr_input_transform *ret = U_TYPED_ARRAY_CALLOC(struct oxr_input_transform, num_transforms);
	memcpy(ret, transforms, sizeof(*ret) * num_transforms);
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
                                 size_t *out_num_transforms)
{
	struct oxr_input_transform chain[OXR_MAX_INPUT_TRANSFORMS] = {0};

	oxr_slog(slog, "\t\tAdding transform from '%s' to '%s'\n", xr_action_type_to_str(result_type),
	         xrt_input_type_to_str(input_type));

	struct oxr_input_transform *current_xform = &(chain[0]);
	if (!oxr_input_transform_init_root(current_xform, input_type)) {
		*out_num_transforms = 0;
		*out_transforms = NULL;
		return false;
	}

	bool identity = (result_type == XR_ACTION_TYPE_POSE_INPUT && input_type == XRT_INPUT_TYPE_POSE) ||
	                oxr_type_matches_xrt(current_xform->result_type, result_type);

	if (identity) {
		// No transform needed, just return identity to keep this alive.
		*out_num_transforms = 1;
		*out_transforms = oxr_input_transform_clone_chain(chain, 1);
		oxr_slog(slog, "\t\t\tUsing identity transform for input.\n");
		return true;
	}

	// We start over here.
	size_t num_transforms = 0;
	while (!oxr_type_matches_xrt(current_xform->result_type, result_type)) {
		if (num_transforms >= OXR_MAX_INPUT_TRANSFORMS) {
			// Couldn't finish the transform to the desired type.
			oxr_slog(slog,
			         "\t\t\tSeem to have gotten into a loop, "
			         "trying to make a rule to transform.\n",
			         action_name, bound_path_string);
			*out_num_transforms = 0;
			*out_transforms = NULL;
			return false;
		}

		struct oxr_input_transform *new_xform = &(chain[num_transforms]);
		if (!extend_transform_array(log, slog, new_xform, current_xform, result_type, bound_path_string)) {
			// Error has already been logged.

			*out_num_transforms = 0;
			*out_transforms = NULL;
			return false;
		}

		num_transforms++;
		current_xform = new_xform;
	}

	*out_num_transforms = num_transforms;
	*out_transforms = oxr_input_transform_clone_chain(chain, num_transforms);

	return true;
}
