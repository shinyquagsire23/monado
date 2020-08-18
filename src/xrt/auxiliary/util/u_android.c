// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for Android-specific global state.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#include "u_android.h"

#include <stddef.h>

/*!
 * @todo Do we need locking here? Do we need to create global refs for the
 * supplied jobjects?
 */
static struct
{
	struct _JavaVM *vm;
	void *activity;
	void *context;
} android_globals = {NULL, NULL, NULL};

void
u_android_store_vm_and_activity(struct _JavaVM *vm, void *activity)
{
	android_globals.vm = vm;
	android_globals.activity = activity;
}

void
u_android_store_vm_and_context(struct _JavaVM *vm, void *context)
{

	android_globals.vm = vm;
	android_globals.context = context;
}

struct _JavaVM *
u_android_get_vm()
{
	return android_globals.vm;
}

void *
u_android_get_activity()
{
	return android_globals.activity;
}

void *
u_android_get_context()
{
	void *ret = android_globals.context;
	if (ret == NULL) {
		ret = android_globals.activity;
	}
	return ret;
}
