// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Function for loading Java code from a package.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

#pragma once

#include <xrt/xrt_config_os.h>

#ifdef XRT_OS_ANDROID

#ifdef __cplusplus
extern "C" {
#endif

struct _JavaVM;

/*!
 * Load a named class from a named package.
 *
 * @param vm Java VM pointer
 * @param pkgname Package name
 * @param application_context An android.content.Context jobject, cast to
 * `void *`.
 * @param classname A fully-qualified Java class name, delimited with ".",
 * which will be loaded using java.lang.Class.forName()
 *
 * @return The jobject for the java.lang.Class you requested (cast to a
 * `void *`), or NULL if there was an error.
 */
void *
android_load_class_from_package(struct _JavaVM *vm,
                                const char *pkgname,
                                void *application_context,
                                const char *classname);

#ifdef __cplusplus
}
#endif

#endif // XRT_OS_ANDROID
