// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for adding a new Surface to an activity and otherwise
 *         interacting with an Android View.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

#pragma once

#include <xrt/xrt_config_os.h>

#ifdef XRT_OS_ANDROID

#include <android/native_window.h>

#ifdef __cplusplus
extern "C" {
#endif

struct _JNIEnv;
struct _JavaVM;

struct xrt_android_display_metrics
{
	int width_pixels;
	int height_pixels;
	int density_dpi;
	float density;
	float scaled_density;
	float xdpi;
	float ydpi;
};

/*!
 * Opaque type representing a custom surface added to an activity, and the async
 * operation to perform this adding.
 *
 * @note You must keep this around for as long as you're using the surface.
 */
struct android_custom_surface;

/*!
 * Start adding a custom surface to an activity.
 *
 * This is an asynchronous operation, so this creates an opaque pointer for you
 * to check on the results and maintain a reference to the result.
 *
 * Uses org.freedesktop.monado.auxiliary.MonadoView
 *
 * @param vm Java VM pointer
 * @param activity An android.app.Activity jobject, cast to
 * `void *`.
 *
 * @return An opaque handle for monitoring this operation and referencing the
 * surface, or NULL if there was an error.
 *
 * @public @memberof android_custom_surface
 */
struct android_custom_surface *
android_custom_surface_async_start(struct _JavaVM *vm, void *activity);

/*!
 * Destroy the native handle for the custom surface.
 *
 * Depending on the state, this may not necessarily destroy the underlying
 * surface, if other references exist. However, a flag will be set to indicate
 * that native code is done using it.
 *
 * @param ptr_custom_surface Pointer to the opaque pointer: will be set to NULL.
 *
 * @public @memberof android_custom_surface
 */
void
android_custom_surface_destroy(struct android_custom_surface **ptr_custom_surface);

/*!
 * Get the ANativeWindow pointer corresponding to the added Surface, if
 * available, waiting up to the specified duration.
 *
 * This may return NULL because the underlying operation is asynchronous.
 *
 * @public @memberof android_custom_surface
 */
ANativeWindow *
android_custom_surface_wait_get_surface(struct android_custom_surface *custom_surface, uint64_t timeout_ms);

bool
android_custom_surface_get_display_metrics(struct _JavaVM *vm,
                                           void *activity,
                                           struct xrt_android_display_metrics *out_metrics);

#ifdef __cplusplus
}
#endif

#endif // XRT_OS_ANDROID
