// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tiny JSON wrapper around cJSON.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#include "util/u_json.h"
#ifndef XRT_HAVE_SYSTEM_CJSON
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include "util/u_logging.h"

#include <assert.h>
#include <stdio.h>

#ifndef XRT_HAVE_SYSTEM_CJSON
// This includes the c file completely.
#include "cjson/cJSON.c"
#endif


/*!
 * Less typing.
 */
static inline const cJSON *
get(const cJSON *json, const char *f)
{
	return cJSON_GetObjectItemCaseSensitive(json, f);
}

const cJSON *
u_json_get(const cJSON *json, const char *f)
{
	return get(json, f);
}

bool
u_json_get_string_into_array(const cJSON *json, char *out_str, size_t max_size)
{
	assert(out_str != NULL);

	if (!json) {
		return false;
	}
	if (!cJSON_IsString(json)) {
		return false;
	}

	int ret = snprintf(out_str, max_size, "%s", json->valuestring);
	if (ret < 0) {
		U_LOG_E("Printing string failed: %d", ret);
		return false;
	}
	if ((size_t)ret < max_size) {
		return true;
	}
	U_LOG_E("String size %d is bigger than available %zu", ret, max_size);
	return false;
}

bool
u_json_get_bool(const cJSON *json, bool *out_bool)
{
	assert(out_bool != NULL);

	if (!json) {
		return false;
	}
	if (!cJSON_IsBool(json)) {
		return false;
	}

	*out_bool = json->valueint;

	return true;
}

bool
u_json_get_int(const cJSON *json, int *out_int)
{
	assert(out_int != NULL);

	if (!json) {
		return false;
	}
	if (!cJSON_IsNumber(json)) {
		return false;
	}

	*out_int = json->valueint;

	return true;
}

bool
u_json_get_double(const cJSON *json, double *out_double)
{
	assert(out_double != NULL);

	if (!json) {
		return false;
	}
	if (!cJSON_IsNumber(json)) {
		return false;
	}
	*out_double = json->valuedouble;
	return true;
}

bool
u_json_get_float(const cJSON *json, float *out_float)
{
	assert(out_float != NULL);

	double d = 0;
	if (!u_json_get_double(json, &d)) {
		return false;
	}

	*out_float = (float)d;
	return true;
}

bool
u_json_get_vec3(const cJSON *json, struct xrt_vec3 *out_vec3)
{
	assert(out_vec3 != NULL);

	if (!json) {
		return false;
	}
	if (!cJSON_IsObject(json)) {
		return false;
	}

	struct xrt_vec3 ret;
	if (!u_json_get_float(get(json, "x"), &ret.x)) {
		return false;
	}
	if (!u_json_get_float(get(json, "y"), &ret.y)) {
		return false;
	}
	if (!u_json_get_float(get(json, "z"), &ret.z)) {
		return false;
	}

	*out_vec3 = ret;

	return true;
}

bool
u_json_get_vec3_array(const cJSON *json, struct xrt_vec3 *out_vec3)
{
	assert(out_vec3 != NULL);

	if (!json) {
		return false;
	}
	if (!cJSON_IsArray(json)) {
		return false;
	}

	if (cJSON_GetArraySize(json) != 3) {
		return false;
	}

	float array[3] = {0, 0, 0};
	const cJSON *item = NULL;
	size_t i = 0;
	cJSON_ArrayForEach(item, json)
	{
		assert(cJSON_IsNumber(item));
		array[i] = (float)item->valuedouble;
		++i;
		if (i == 3) {
			break;
		}
	}

	out_vec3->x = array[0];
	out_vec3->y = array[1];
	out_vec3->z = array[2];

	return true;
}

bool
u_json_get_quat(const cJSON *json, struct xrt_quat *out_quat)
{
	assert(out_quat != NULL);

	if (!json) {
		return false;
	}
	if (!cJSON_IsObject(json)) {
		return false;
	}

	struct xrt_quat ret;
	if (!u_json_get_float(get(json, "w"), &ret.w)) {
		return false;
	}
	if (!u_json_get_float(get(json, "x"), &ret.x)) {
		return false;
	}
	if (!u_json_get_float(get(json, "y"), &ret.y)) {
		return false;
	}
	if (!u_json_get_float(get(json, "z"), &ret.z)) {
		return false;
	}

	*out_quat = ret;

	return true;
}

size_t
u_json_get_float_array(const cJSON *json_array, float *out_array, size_t max_size)
{
	assert(out_array != NULL);

	if (!json_array) {
		return 0;
	}
	if (!cJSON_IsArray(json_array)) {
		return 0;
	}

	size_t i = 0;
	const cJSON *elt;
	cJSON_ArrayForEach(elt, json_array)
	{
		if (i >= max_size) {
			break;
		}

		if (!u_json_get_float(elt, &out_array[i])) {
			U_LOG_W(
			    "u_json_get_float_array got a non-number in a "
			    "numeric array");
			return i;
		}

		i++;
	}

	return i;
}

size_t
u_json_get_double_array(const cJSON *json_array, double *out_array, size_t max_size)
{
	assert(out_array != NULL);

	if (!json_array) {
		return 0;
	}
	if (!cJSON_IsArray(json_array)) {
		return 0;
	}

	size_t i = 0;
	const cJSON *elt;
	cJSON_ArrayForEach(elt, json_array)
	{
		if (i >= max_size) {
			break;
		}

		if (!u_json_get_double(elt, &out_array[i])) {
			U_LOG_W(
			    "u_json_get_double_array got a non-number in a "
			    "numeric array");
			return i;
		}

		i++;
	}

	return i;
}

bool
u_json_get_matrix_3x3(const cJSON *json, struct xrt_matrix_3x3 *out_matrix)
{
	assert(out_matrix != NULL);

	if (!json) {
		return false;
	}
	if (cJSON_GetArraySize(json) != 3) {
		return false;
	}

	size_t total = 0;
	const cJSON *vec = NULL;
	cJSON_ArrayForEach(vec, json)
	{
		assert(cJSON_GetArraySize(vec) == 3);
		const cJSON *elem = NULL;
		cJSON_ArrayForEach(elem, vec)
		{
			// Just in case.
			if (total >= 9) {
				break;
			}

			assert(cJSON_IsNumber(elem));
			out_matrix->v[total++] = (float)elem->valuedouble;
		}
	}

	return true;
}
