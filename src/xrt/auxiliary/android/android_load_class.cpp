// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementations for loading Java code from a package.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

#include "android_load_class.hpp"

#include "util/u_logging.h"

#include "wrap/android.content.h"
#include "wrap/dalvik.system.h"

#include "jni.h"

using wrap::android::content::Context;
using wrap::android::content::pm::ApplicationInfo;
using wrap::android::content::pm::PackageManager;

ApplicationInfo
getAppInfo(std::string const &packageName, jobject application_context)
{
	try {
		auto context = Context{application_context};
		if (context.isNull()) {
			U_LOG_E("getAppInfo: application_context was null");
			return {};
		}
		auto packageManager = PackageManager{context.getPackageManager()};
		if (packageManager.isNull()) {
			U_LOG_E(
			    "getAppInfo: "
			    "application_context.getPackageManager() returned "
			    "null");
			return {};
		}
		auto packageInfo = packageManager.getPackageInfo(
		    packageName, PackageManager::GET_META_DATA | PackageManager::GET_SHARED_LIBRARY_FILES);

		if (packageInfo.isNull()) {
			U_LOG_E(
			    "getAppInfo: "
			    "application_context.getPackageManager()."
			    "getPackaegInfo() returned null");
			return {};
		}
		return packageInfo.getApplicationInfo();
	} catch (std::exception const &e) {
		U_LOG_E("Could get App Info: %s", e.what());
		return {};
	}
}

wrap::java::lang::Class
loadClassFromPackage(ApplicationInfo applicationInfo, jobject application_context, const char *clazz_name)
{
	auto context = Context{application_context}.getApplicationContext();
	auto pkgContext = context.createPackageContext(
	    applicationInfo.getPackageName(), Context::CONTEXT_IGNORE_SECURITY | Context::CONTEXT_INCLUDE_CODE);

	// Not using ClassLoader.loadClass because it expects a /-delimited
	// class name, while we have a .-delimited class name.
	// This does work
	wrap::java::lang::ClassLoader pkgClassLoader = pkgContext.getClassLoader();

	try {
		auto loadedClass = pkgClassLoader.loadClass(std::string(clazz_name));
		if (loadedClass.isNull()) {
			U_LOG_E("Could not load class for name %s", clazz_name);
			return wrap::java::lang::Class();
		}

		return loadedClass;
	} catch (std::exception const &e) {
		U_LOG_E("Could load class '%s' forName: %s", clazz_name, e.what());
		return wrap::java::lang::Class();
	}
}

void *
android_load_class_from_package(struct _JavaVM *vm,
                                const char *pkgname,
                                void *application_context,
                                const char *classname)
{
	jni::init(vm);
	Context context((jobject)application_context);
	auto info = getAppInfo(pkgname, (jobject)application_context);
	if (info.isNull()) {
		U_LOG_E("Could not get application info for package '%s'", pkgname);
		return nullptr;
	}
	auto clazz = loadClassFromPackage(info, (jobject)application_context, classname);
	if (clazz.isNull()) {
		U_LOG_E("Could not load class '%s' from package '%s'", classname, pkgname);
		return nullptr;
	}
	return clazz.object().getHandle();
}
