// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for Android-specific global state.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

#include "android_globals.h"

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
	struct _ANativeWindow *window;
} android_globals = {NULL, NULL, NULL};

void
android_globals_store_vm_and_activity(struct _JavaVM *vm, void *activity)
{
	android_globals.vm = vm;
	android_globals.activity = activity;
}

void
android_globals_store_vm_and_context(struct _JavaVM *vm, void *context)
{

	android_globals.vm = vm;
	android_globals.context = context;
}

void
android_globals_store_window(struct _ANativeWindow *window)
{
	android_globals.window = window;
}

struct _ANativeWindow *
android_globals_get_window()
{
	return android_globals.window;
}

struct _JavaVM *
android_globals_get_vm()
{
	return android_globals.vm;
}

void *
android_globals_get_activity()
{
	return android_globals.activity;
}

void *
android_globals_get_context()
{
	void *ret = android_globals.context;
	if (ret == NULL) {
		ret = android_globals.activity;
	}
	return ret;
}
