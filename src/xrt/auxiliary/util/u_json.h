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

#include "cjson/cJSON.h"


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/*!
 * @brief Parse up to max_size doubles from a JSON array.
 *
 * @return the number of elements set.
 */
size_t
u_json_get_double_array(const cJSON *json_array,
                        double *out_array,
                        size_t max_size);
#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
