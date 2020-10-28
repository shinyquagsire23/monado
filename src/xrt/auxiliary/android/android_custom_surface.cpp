// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of native code for Android custom surface.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup aux_android
 */

#include "android_custom_surface.h"
#include "android_load_class.hpp"

#include "xrt/xrt_config_android.h"
#include "util/u_logging.h"

#include "wrap/android.app.h"
#include "wrap/android.view.h"

#include <android/native_window_jni.h>

#include "jni.h"

using wrap::android::app::Activity;
using wrap::android::view::SurfaceHolder;


struct android_custom_surface
{
	Activity activity;
	jni::Class monadoViewClass;
	jni::Object monadoView;
	jni::method_t waitGetSurfaceHolderMethod;
};

constexpr auto FULLY_QUALIFIED_CLASSNAME =
    "org.freedesktop.monado.auxiliary.MonadoView";

struct android_custom_surface *
android_custom_surface_async_start(struct _JavaVM *vm, void *activity)
{
	jni::init(vm);
	jni::method_t attachToActivity;
	try {
		std::unique_ptr<android_custom_surface> ret =
		    std::make_unique<android_custom_surface>();

		ret->activity = Activity((jobject)activity);
		auto info = getAppInfo(XRT_ANDROID_PACKAGE, (jobject)activity);
		if (info.isNull()) {
			U_LOG_E(
			    "Could not get application info for package '%s'",
			    "org.freedesktop.monado.openxr_runtime");
			return nullptr;
		}

		auto clazz = loadClassFromPackage(info, (jobject)activity,
		                                  FULLY_QUALIFIED_CLASSNAME);

		if (clazz.isNull()) {
			U_LOG_E("Could not load class '%s' from package '%s'",
			        FULLY_QUALIFIED_CLASSNAME, XRT_ANDROID_PACKAGE);
			return nullptr;
		}

		// the 0 is to avoid this being considered "temporary" and to
		// create a global ref.
		ret->monadoViewClass =
		    jni::Class((jclass)clazz.object().getHandle(), 0);

		if (ret->monadoViewClass.isNull()) {
			U_LOG_E("monadoViewClass was null");
			return nullptr;
		}

		std::string clazz_name = ret->monadoViewClass.getName();
		if (clazz_name != FULLY_QUALIFIED_CLASSNAME) {
			U_LOG_E("Unexpected class name: %s",
			        clazz_name.c_str());
			return nullptr;
		}

		if (ret->monadoViewClass.isNull()) {
			U_LOG_E("MonadoView was null.");
			return nullptr;
		}

		ret->waitGetSurfaceHolderMethod =
		    ret->monadoViewClass.getMethod(
		        "waitGetSurfaceHolder",
		        "(I)Landroid/view/SurfaceHolder;");

		attachToActivity = ret->monadoViewClass.getStaticMethod(
		    "attachToActivity",
		    "(Landroid/app/Activity;)Lorg/freedesktop/monado/auxiliary/"
		    "MonadoView;");

		ret->monadoView = ret->monadoViewClass.call<jni::Object>(
		    attachToActivity, ret->activity.object());
		return ret.release();
	} catch (std::exception const &e) {

		U_LOG_E(
		    "Could not start attaching our custom surface to activity: "
		    "%s",
		    e.what());
		return nullptr;
	}
}


void
android_custom_surface_destroy(
    struct android_custom_surface **ptr_custom_surface)
{
	if (ptr_custom_surface == NULL) {
		return;
	}
	struct android_custom_surface *custom_surface = *ptr_custom_surface;
	if (custom_surface == NULL) {
		return;
	}
	delete custom_surface;
	*ptr_custom_surface = NULL;
}

ANativeWindow *
android_custom_surface_wait_get_surface(
    struct android_custom_surface *custom_surface, uint64_t timeout_ms)
{
	jni::Object holder = nullptr;
	try {

		holder = custom_surface->monadoView.call<jni::Object>(
		    custom_surface->waitGetSurfaceHolderMethod,
		    (int)timeout_ms);

	} catch (std::exception const &e) {
		// do nothing right now.
		U_LOG_E(
		    "Could not wait for our custom surface: "
		    "%s",
		    e.what());
		return nullptr;
	}

	SurfaceHolder surfaceHolder(holder);
	if (surfaceHolder.isNull()) {
		return nullptr;
	}
	auto surf = surfaceHolder.getSurface();
	if (surf.isNull()) {
		return nullptr;
	}
	return ANativeWindow_fromSurface(jni::env(), surf.object().getHandle());
}
