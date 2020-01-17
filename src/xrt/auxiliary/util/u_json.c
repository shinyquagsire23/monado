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

#include "cjson/cJSON.c"

size_t
u_json_get_double_array(const cJSON *json_array,
                        double *out_array,
                        size_t max_size)
{
	if (!json_array) {
		return 0;
	}
	if (!out_array) {
		return 0;
	}
	if (!cJSON_IsArray(json_array)) {
		return 0;
	}
	if (max_size == 0) {
		return 0;
	}
	size_t i = 0;
	const cJSON *elt;
	cJSON_ArrayForEach(elt, json_array)
	{
		if (!cJSON_IsNumber(elt)) {
			fprintf(stderr,
			        "warning: u_json_get_double_array got a "
			        "non-number in a numeric array");
			return i;
		}
		out_array[i] = elt->valuedouble;
		++i;
		if (i == max_size) {
			break;
		}
	}
	return i;
}
