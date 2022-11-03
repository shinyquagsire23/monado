// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds binding related functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup oxr_main
 */

#include "oxr_objects.h"


/*
 *
 * Helper functions.
 *
 */

static void
destroy_callback(void *item, void *priv)
{
	free(item);
}


/*
 *
 * 'Exported' functions.
 *
 */

bool
oxr_dpad_state_init(struct oxr_dpad_state *state)
{
	if (u_hashmap_int_create(&state->uhi) < 0) {
		return false;
	}

	return true;
}

struct oxr_dpad_entry *
oxr_dpad_state_get(struct oxr_dpad_state *state, uint64_t key)
{
	void *ptr = NULL;
	u_hashmap_int_find(state->uhi, key, &ptr);
	return (struct oxr_dpad_entry *)ptr;
}

struct oxr_dpad_entry *
oxr_dpad_state_get_or_add(struct oxr_dpad_state *state, uint64_t key)
{
	struct oxr_dpad_entry *e = oxr_dpad_state_get(state, key);
	if (e == NULL) {
		e = U_TYPED_CALLOC(struct oxr_dpad_entry);
		XRT_MAYBE_UNUSED int ret = u_hashmap_int_insert(state->uhi, key, (void *)e);
		assert(ret >= 0);
	}

	return e;
}

void
oxr_dpad_state_deinit(struct oxr_dpad_state *state)
{
	if (state != NULL && state->uhi != NULL) {
		u_hashmap_int_clear_and_call_for_each(state->uhi, destroy_callback, NULL);
		u_hashmap_int_destroy(&state->uhi);
	}
}
