// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tiny JSON wrapper around cJSON.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#include "u_json.h"
#include <assert.h>

// This includes the c file completely.
#include "cjson/cJSON.c"


/*!
 * Less typing.
 */
static inline const cJSON *
get(const cJSON *json, const char *f)
{
	return cJSON_GetObjectItemCaseSensitive(json, f);
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
		return false;
	} else if ((size_t)ret < max_size) {
		return true;
	} else {
		return false;
	}
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
u_json_get_float_array(const cJSON *json_array,
                       float *out_array,
                       size_t max_size)
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
			fprintf(stderr,
			        "warning: u_json_get_float_array got a "
			        "non-number in a numeric array");
			return i;
		}

		i++;
	}

	return i;
}

size_t
u_json_get_double_array(const cJSON *json_array,
                        double *out_array,
                        size_t max_size)
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
			fprintf(stderr,
			        "warning: u_json_get_double_array got a "
			        "non-number in a numeric array");
			return i;
		}

		i++;
	}

	return i;
}
