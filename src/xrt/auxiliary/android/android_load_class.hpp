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

#include "wrap/android.app.h"

#ifdef XRT_OS_ANDROID

//! C++-only functionality in the Android auxiliary library
namespace xrt::auxiliary::android {

using wrap::android::content::pm::ApplicationInfo;

/*!
 * @note Starting from Android 11, NameNotFoundException exception is thrown if application doesn't
 * specify either <queries> or "android.permission.QUERY_ALL_PACKAGES".
 * See https://developer.android.com/training/package-visibility for detail.
 */
ApplicationInfo
getAppInfo(std::string const &packageName, jobject application_context);

/*!
 * @note Starting from Android 11, NameNotFoundException exception is thrown if application doesn't
 * specify either <queries> or "android.permission.QUERY_ALL_PACKAGES".
 * See https://developer.android.com/training/package-visibility for detail.
 */
wrap::java::lang::Class
loadClassFromPackage(ApplicationInfo applicationInfo, jobject application_context, const char *clazz_name);

/*!
 * Loading class from given apk path.
 *
 * @param application_context Context.
 * @param apk_path Path to apk.
 * @param clazz_name Name of class to be loaded.
 * @return Class object.
 */
wrap::java::lang::Class
loadClassFromApk(jobject application_context, const char *apk_path, const char *clazz_name);

/*!
 * Loading class from runtime apk.
 *
 * @param application_context Context.
 * @param clazz_name Name of class to be loaded.
 * @return Class object.
 */
wrap::java::lang::Class
loadClassFromRuntimeApk(jobject application_context, const char *clazz_name);

} // namespace xrt::auxiliary::android

#endif // XRT_OS_ANDROID
