// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Very small misc utils.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Allocate and zero the give size and casts the memory into a pointer of the
 * given type.
 *
 * @ingroup aux_util
 */
#define U_CALLOC_WITH_CAST(TYPE, SIZE) ((TYPE *)calloc(1, SIZE))

/*!
 * Allocate and zero the space required for some type, and cast the return type
 * appropriately.
 *
 * @ingroup aux_util
 */
#define U_TYPED_CALLOC(TYPE) ((TYPE *)calloc(1, sizeof(TYPE)))

/*!
 * Allocate and zero the space required for some type, and cast the return type
 * appropriately.
 *
 * @ingroup aux_util
 */
#define U_TYPED_ARRAY_CALLOC(TYPE, COUNT)                                      \
	((TYPE *)calloc((COUNT), sizeof(TYPE)))


#ifdef __cplusplus
}
#endif
