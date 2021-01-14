// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Hashset struct header.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @struct u_hashset
 * @ingroup aux_util
 *
 * Kind of bespoke hashset implementation, where the user is responsible for
 * allocating and freeing the items themselves.
 *
 * This allows embedding the @ref u_hashset_item at the end of structs.
 */
struct u_hashset;

/*!
 * A embeddable hashset item, note that the string directly follows the
 * @ref u_hashset_item.
 *
 * @ingroup aux_util
 */
struct u_hashset_item
{
	size_t hash;
	size_t length;

#ifdef __cplusplus
	inline const char *
	c_str()
	{
		return (const char *)&this[1];
	}
#else
	const char c_str[];
#endif
};

typedef void (*u_hashset_callback)(struct u_hashset_item *item, void *priv);

int
u_hashset_create(struct u_hashset **out_hashset);

int
u_hashset_destroy(struct u_hashset **hs);

int
u_hashset_find_str(struct u_hashset *hs, const char *str, size_t length, struct u_hashset_item **out_item);

int
u_hashset_find_c_str(struct u_hashset *hs, const char *c_str, struct u_hashset_item **out_item);

int
u_hashset_create_and_insert_str(struct u_hashset *hs, const char *str, size_t length, struct u_hashset_item **out_item);

int
u_hashset_create_and_insert_str_c(struct u_hashset *hs, const char *c_str, struct u_hashset_item **out_item);

int
u_hashset_insert_item(struct u_hashset *hs, struct u_hashset_item *item);

int
u_hashset_erase_item(struct u_hashset *hs, struct u_hashset_item *item);

int
u_hashset_erase_str(struct u_hashset *hs, const char *str, size_t length);

int
u_hashset_erase_c_str(struct u_hashset *hs, const char *c_str);

/*!
 * First clear the hashset and then call the given callback with each item that
 * was in the hashset.
 *
 * @ingroup aux_util
 */
void
u_hashset_clear_and_call_for_each(struct u_hashset *hs, u_hashset_callback cb, void *priv);


#ifdef __cplusplus
}
#endif
