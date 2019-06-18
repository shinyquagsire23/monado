// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Very small misc utils.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <stdlib.h> // for calloc
#include <string.h> // for memset

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Allocate and zero the give size and casts the memory into a pointer of the
 * given type.
 *
 * Use instead of a bare calloc, but only when U_TYPED_CALLOC and
 * U_TYPED_ARRAY_CALLOC do not meet your needs.
 *
 * @ingroup aux_util
 */
#define U_CALLOC_WITH_CAST(TYPE, SIZE) ((TYPE *)calloc(1, SIZE))

/*!
 * Allocate and zero the space required for some type, and cast the return type
 * appropriately.
 *
 * Use instead of a bare calloc when allocating a single structure.
 *
 * @ingroup aux_util
 */
#define U_TYPED_CALLOC(TYPE) ((TYPE *)calloc(1, sizeof(TYPE)))

/*!
 * Allocate and zero the space required for some type, and cast the return type
 * appropriately.
 *
 * Use instead of a bare calloc when allocating an array of a type.
 * This includes allocating C strings: pass char as the type.
 *
 * @ingroup aux_util
 */
#define U_TYPED_ARRAY_CALLOC(TYPE, COUNT)                                      \
	((TYPE *)calloc((COUNT), sizeof(TYPE)))

/*!
 * Zeroes the correct amount of memory based on the type pointed-to by the
 * argument.
 *
 * Use instead of memset(..., 0, ...) on a structure or pointer to structure.
 *
 * @ingroup aux_util
 */
#define U_ZERO(PTR) memset((PTR), 0, sizeof(*(PTR)))

/*!
 * Zeroes the correct amount of memory based on the type and size of the static
 * array named in the argument.
 *
 * Use instead of memset(..., 0, ...) on an array.
 *
 * @ingroup aux_util
 */
#define U_ZERO_ARRAY(ARRAY) memset((ARRAY), 0, sizeof(ARRAY))

#ifdef __cplusplus
}
#endif
