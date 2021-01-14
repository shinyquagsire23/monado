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

using wrap::android::content::pm::ApplicationInfo;

ApplicationInfo
getAppInfo(std::string const &packageName, jobject application_context);

wrap::java::lang::Class
loadClassFromPackage(ApplicationInfo applicationInfo, jobject application_context, const char *clazz_name);

#endif // XRT_OS_ANDROID
