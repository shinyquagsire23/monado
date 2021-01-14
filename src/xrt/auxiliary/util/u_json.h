// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tiny JSON wrapper around cJSON header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_defines.h"

#include "cjson/cJSON.h"


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/*!
 * @brief Get a JSON object by string from a JSON object.
 *
 * @return The JSON object with the given name if successful, NULL if not.
 */
const cJSON *
u_json_get(const cJSON *json, const char *f);

/*!
 * @brief Parse a string from a JSON object into a char array.
 *
 * @return true if successful, false if string does not fit in
 *         array or any other error.
 */
bool
u_json_get_string_into_array(const cJSON *json, char *out, size_t max_size);

/*!
 * @brief Parse an bool from a JSON object.
 *
 * @return true if successful, false if not.
 */
bool
u_json_get_bool(const cJSON *json, bool *out_bool);

/*!
 * @brief Parse an int from a JSON object.
 *
 * @return true if successful, false if not.
 */
bool
u_json_get_int(const cJSON *json, int *out_int);

/*!
 * @brief Parse a float from a JSON object.
 *
 * @return true if successful, false if not.
 */
bool
u_json_get_float(const cJSON *json, float *out_float);

/*!
 * @brief Parse a double from a JSON object.
 *
 * @return true if successful, false if not.
 */
bool
u_json_get_double(const cJSON *json, double *out_double);

/*!
 * @brief Parse a vec3 from a JSON object.
 *
 * @return true if successful, false if not.
 */
bool
u_json_get_vec3(const cJSON *json, struct xrt_vec3 *out_vec3);

/*!
 * @brief Parse a vec3 from a JSON array.
 *
 * @return true if successful, false if not.
 */
bool
u_json_get_vec3_array(const cJSON *json, struct xrt_vec3 *out_vec3);

/*!
 * @brief Parse a quaternion from a JSON object.
 *
 * @return true if successful, false if not.
 */
bool
u_json_get_quat(const cJSON *json, struct xrt_quat *out_quat);

/*!
 * @brief Parse up to max_size floats from a JSON array.
 *
 * @return the number of elements set.
 */
size_t
u_json_get_float_array(const cJSON *json_array, float *out_array, size_t max_size);

/*!
 * @brief Parse up to max_size doubles from a JSON array.
 *
 * @return the number of elements set.
 */
size_t
u_json_get_double_array(const cJSON *json_array, double *out_array, size_t max_size);

/*!
 * @brief Parse a matrix_3x3 from a JSON object.
 *
 * @return true if successful, false if not.
 */
bool
u_json_get_matrix_3x3(const cJSON *json, struct xrt_matrix_3x3 *out_matrix);


#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
