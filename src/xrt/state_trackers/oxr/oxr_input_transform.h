// Copyright 2018-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Defines ways of performing (possibly multi-step) conversions of input
 * data.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup oxr_input_transform
 */

#pragma once

#include "xrt/xrt_device.h"

// we need no platform-specific defines from OpenXR.
#include "openxr/openxr.h"

#ifdef __cplusplus
extern "C" {
#endif

struct oxr_logger;
struct oxr_sink_logger;
struct oxr_action;
struct oxr_action_cache;

/*!
 * @defgroup oxr_input_transform OpenXR input transformation
 *
 *
 * @ingroup oxr_input
 * @{
 */

/*!
 * Tag for the input transform
 *
 * @see oxr_input_transform
 */
enum oxr_input_transform_type
{
	/*!
	 * Invalid value, so that zero-initialization without further assignment
	 * is caught.
	 */
	INPUT_TRANSFORM_INVALID = 0,

	/*!
	 * Do not modify the input.
	 *
	 * This is only used as the root/head transform, to set the initial
	 * type.
	 */
	INPUT_TRANSFORM_IDENTITY,

	/*!
	 * Get the X component of a 2D float input of any range.
	 */
	INPUT_TRANSFORM_VEC2_GET_X,

	/*!
	 * Get the Y component of a 2D float input of any range.
	 */
	INPUT_TRANSFORM_VEC2_GET_Y,

	/*!
	 * Apply a threshold to any 1D float input to make a bool.
	 *
	 * This transform type has data:
	 *
	 * @see oxr_input_transform_threshold_data
	 */
	INPUT_TRANSFORM_THRESHOLD,

	/*!
	 * Convert a bool to some range of 1D float input.
	 *
	 * This transform type has data:
	 *
	 * @see oxr_input_transform_bool_to_vec1_data
	 */
	INPUT_TRANSFORM_BOOL_TO_VEC1,
};

struct oxr_input_transform;
/*!
 * Data required for INPUT_TRANSFORM_THRESHOLD
 */
struct oxr_input_transform_threshold_data
{
	//! The "greater-than" threshold value
	float threshold;

	//! If true, values above threshold are false instead of
	//! true
	bool invert;
};

/*!
 * Data required for INPUT_TRANSFORM_BOOL_TO_VEC1
 * @see oxr_input_transform
 * @see INPUT_TRANSFORM_BOOL_TO_VEC1
 */
struct oxr_input_transform_bool_to_vec1_data
{
	//! Value produced if bool is true.
	float true_val;

	//! Value produced if bool is false.
	float false_val;
};

/*!
 * Variant type for input transforms.
 *
 * Some values for @p type do not have any additional data in the union
 */
struct oxr_input_transform
{
	/*!
	 * The type of this transform.
	 *
	 * Some values imply that a member of oxr_input_transform::data is
	 * populated.
	 */
	enum oxr_input_transform_type type;

	//! The type output by this transform.
	enum xrt_input_type result_type;

	union {
		/*!
		 * Populated when oxr_input_transform::type is
		 * INPUT_TRANSFORM_THRESHOLD
		 */
		struct oxr_input_transform_threshold_data threshold;
		/*!
		 * Populated when oxr_input_transform::type is
		 * INPUT_TRANSFORM_BOOL_TO_VEC1
		 */
		struct oxr_input_transform_bool_to_vec1_data bool_to_vec1;
	} data;
};

/*!
 * An input value enum with the associated tag required to interpret it.
 */
struct oxr_input_value_tagged
{
	enum xrt_input_type type;
	union xrt_input_value value;
};

/*!
 * Destroy an array of input transforms.
 *
 * Performs null check and sets to NULL.
 *
 * @public @memberof oxr_input_transform
 */
void
oxr_input_transform_destroy(struct oxr_input_transform **transform_ptr);

/*!
 * Apply an array of input transforms.
 *
 * @param[in] transforms An array of input transforms
 * @param[in] num_transforms The number of elements in @p transform
 * @param[in] input The input value and type
 * @param[out] out The transformed value and type
 *
 * @returns false if there was a type mismatch
 * @public @memberof oxr_input_transform
 */
bool
oxr_input_transform_process(const struct oxr_input_transform *transforms,
                            size_t num_transforms,
                            const struct oxr_input_value_tagged *input,
                            struct oxr_input_value_tagged *out);

/*!
 * Allocate an identity transform serving as the root/head of the transform
 * chain.
 *
 * Usually called automatically by @ref oxr_input_transform_create_chain
 *
 * @param[in,out] transform A pointer to the @ref oxr_input_transform struct to
 * initialize.
 * @param[in] input_type The native input type from the device
 *
 * @public @memberof oxr_input_transform
 */
bool
oxr_input_transform_init_root(struct oxr_input_transform *transform, const enum xrt_input_type input_type);

/*!
 * Allocate a transform to get the X component of a Vec2.
 *
 * Usually called automatically by @ref oxr_input_transform_create_chain
 *
 * @param[in,out] transform A pointer to the @ref oxr_input_transform struct to
 * initialize.
 * @param[in] parent The preceding transform
 *
 * @pre parent->result_type is @ref XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE
 *
 * @public @memberof oxr_input_transform
 */
bool
oxr_input_transform_init_vec2_get_x(struct oxr_input_transform *transform, const struct oxr_input_transform *parent);

/*!
 * Allocate a transform to get the Y component of a Vec2.
 *
 * Usually called automatically by @ref oxr_input_transform_create_chain
 *
 * @param[in,out] transform A pointer to the @ref oxr_input_transform struct to
 * initialize.
 * @param[in] parent The preceding transform
 *
 * @pre parent->result_type is @ref XRT_INPUT_TYPE_VEC2_MINUS_ONE_TO_ONE
 *
 * @public @memberof oxr_input_transform
 */
bool
oxr_input_transform_init_vec2_get_y(struct oxr_input_transform *transform, const struct oxr_input_transform *parent);

/*!
 * Allocate a transform to threshold a float to a bool.
 *
 * Usually called automatically by @ref oxr_input_transform_create_chain
 *
 * @param[in,out] transform A pointer to the @ref oxr_input_transform struct to
 * initialize.
 * @param[in] parent The preceding transform
 * @param[in] threshold Threshold value to use
 * @param[in] invert If true, condition is "value <= threshold" instead of
 * "value > threshold"
 *
 * @pre parent->result_type is @ref XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE or
 * @ref XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE
 *
 * @public @memberof oxr_input_transform
 */
bool
oxr_input_transform_init_threshold(struct oxr_input_transform *transform,
                                   const struct oxr_input_transform *parent,
                                   float threshold,
                                   bool invert);

/*!
 * Allocate a transform to turn a bool into an arbitrary 1D float.
 *
 * Usually called automatically by @ref oxr_input_transform_create_chain
 *
 * @param[in,out] transform A pointer to the @ref oxr_input_transform struct to
 * initialize.
 * @param[in] parent The preceding transform
 * @param[in] result_type Either XRT_INPUT_TYPE_VEC1_ZERO_TO_ONE or
 * XRT_INPUT_TYPE_VEC1_MINUS_ONE_TO_ONE
 * @param[in] true_val Value to return when true
 * @param[in] false_val Value to return when false
 *
 * @pre parent->result_type is XRT_INPUT_TYPE_BOOLEAN
 *
 * @public @memberof oxr_input_transform
 */
bool
oxr_input_transform_init_bool_to_vec1(struct oxr_input_transform *transform,
                                      const struct oxr_input_transform *parent,
                                      enum xrt_input_type result_type,
                                      float true_val,
                                      float false_val);

/*!
 * Create a transform array to convert @p input_type to @p result_type.
 *
 * @todo In the future, this should be configured using knowledge from the
 * device as well as user options.
 *
 * @param[in] log The logger
 * @param[in] slog The sink logger
 * @param[in] input_type The type of input received from the hardware
 * @param[in] result_type The type of input the application requested
 * @param[in] action_name The action name - used for error prints only
 * @param[in] bound_path_string The path name string that has been bound.
 * @param[out] out_transforms A pointer that will be populated with the output
 * array's address, or NULL.
 * @param[out] out_num_transforms Where to populate the array size
 * @return false if not possible
 *
 * @relates oxr_input_transform
 */
bool
oxr_input_transform_create_chain(struct oxr_logger *log,
                                 struct oxr_sink_logger *slog,
                                 enum xrt_input_type input_type,
                                 XrActionType result_type,
                                 const char *action_name,
                                 const char *bound_path_string,
                                 struct oxr_input_transform **out_transforms,
                                 size_t *out_num_transforms);

/*!
 * @}
 */


#ifdef __cplusplus
}
#endif
