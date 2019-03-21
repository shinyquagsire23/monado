// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Very small misc utils.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * Allocate and zero the space required for some type, and cast the return type
 * appropriately.
 */
#define U_TYPED_CALLOC(TYPE) ((TYPE *)calloc(1, sizeof(TYPE)))

/*!
 * Allocate and zero the space required for some type, and cast the return type
 * appropriately.
 */
#define U_TYPED_ARRAY_CALLOC(TYPE, COUNT)                                      \
	((TYPE *)calloc((COUNT), sizeof(TYPE)))


#ifdef __cplusplus
}
#endif
