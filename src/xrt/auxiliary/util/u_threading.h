// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Slightly higher level thread safe helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_util
 */

#pragma once

#include "os/os_threading.h"


struct u_threading_stack
{
	struct os_mutex mutex;

	void **arr;

	size_t length;
	size_t num;
};


static inline void
u_threading_stack_init(struct u_threading_stack *uts)
{
	os_mutex_init(&uts->mutex);
}

static inline void
u_threading_stack_push(struct u_threading_stack *uts, void *ptr)
{
	if (ptr == NULL) {
		return;
	}

	os_mutex_lock(&uts->mutex);

	if (uts->num + 1 > uts->length) {
		uts->length += 8;
		uts->arr = (void **)realloc(uts->arr, uts->length * sizeof(void *));
	}

	uts->arr[uts->num++] = ptr;

	os_mutex_unlock(&uts->mutex);
}

static inline void *
u_threading_stack_pop(struct u_threading_stack *uts)
{
	void *ret = NULL;

	os_mutex_lock(&uts->mutex);

	if (uts->num > 0) {
		ret = uts->arr[--uts->num];
		uts->arr[uts->num] = NULL;
	}

	os_mutex_unlock(&uts->mutex);

	return ret;
}

static inline void *
u_threading_stack_fini(struct u_threading_stack *uts)
{
	void *ret = NULL;

	os_mutex_destroy(&uts->mutex);

	if (uts->arr != NULL) {
		free(uts->arr);
		uts->arr = NULL;
	}

	return ret;
}
