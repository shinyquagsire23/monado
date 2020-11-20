// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for Android-specific global state.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

#pragma once

#include <xrt/xrt_config_os.h>

#ifdef XRT_OS_ANDROID

#ifdef __cplusplus
extern "C" {
#endif

struct _JNIEnv;
struct _JavaVM;
struct _ANativeWindow;

/*!
 * Store the Java VM pointer and the android.app.Activity jobject.
 */
void
android_globals_store_vm_and_activity(struct _JavaVM *vm, void *activity);


/*!
 * Store the Java VM pointer and the android.content.Context jobject.
 */
void
android_globals_store_vm_and_context(struct _JavaVM *vm, void *context);

/*!
 * Retrieve the Java VM pointer previously stored, if any.
 */
struct _JavaVM *
android_globals_get_vm();

/*!
 * Retrieve the android.app.Activity jobject previously stored, if any.
 *
 * For usage, cast the return value to jobject - a typedef whose definition
 * differs between C (a void *) and C++ (a pointer to an empty class)
 */
void *
android_globals_get_activity();

/*!
 * Retrieve the android.content.Context jobject previously stored, if any.
 *
 * Since android.app.Activity is a sub-class of android.content.Context, the
 * activity jobject will be returned if it has been set but the context has not.
 *
 * For usage, cast the return value to jobject - a typedef whose definition
 * differs between C (a void *) and C++ (a pointer to an empty class)
 */
void *
android_globals_get_context();


void
android_globals_store_window(struct _ANativeWindow *window);

struct _ANativeWindow *
android_globals_get_window();

#ifdef __cplusplus
}
#endif

#endif // XRT_OS_ANDROID
