// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>
// Author: Lubosz Sarnecki <lubosz.sarnecki@collabora.com>

#include "android_custom_surface.h"
#include "android_load_class.h"

#include "xrt/xrt_config_android.h"
#include "util/u_logging.h"

#include "wrap/android.app.h"
#include "wrap/android.view.h"

#include <android/native_window_jni.h>

#include "jni.h"

#include <memory>

using wrap::android::app::Activity;
using wrap::android::view::SurfaceHolder;

using wrap::android::content::Context;

struct android_custom_surface
{
	Activity activity;
	jni::Class monadoViewClass;
	jni::Object monadoView;
};

struct android_custom_surface *
android_custom_surface_async_start(struct _JavaVM *vm, void *activity)
{
	jni::init(vm);
	jni::method_t attachToActivity;
	try {
		std::unique_ptr<android_custom_surface> ret =
		    std::make_unique<android_custom_surface>();

		ret->activity = Activity((jobject)activity);
		Context context((jobject)activity);
		auto info = getAppInfo(XRT_ANDROID_PACKAGE, (jobject)activity);
		if (info.isNull()) {
			U_LOG_E(
			    "Could not get application info for package '%s'",
			    "org.freedesktop.monado.openxr_runtime");
			return nullptr;
		}

		auto clazz = loadClassFromPackage(
		    info, (jobject)activity,
		    "org.freedesktop.monado.auxiliary.MonadoView");

		if (clazz.isNull()) {
			U_LOG_E("Could not load class '%s' from package '%s'",
			        "org.freedesktop.monado.auxiliary.MonadoView",
			        XRT_ANDROID_PACKAGE);
			return nullptr;
		}

		ret->monadoViewClass =
		    jni::Class((jclass)clazz.object().getHandle());

		if (ret->monadoViewClass.isNull()) {
			U_LOG_E("monadoViewClass was null");
			return nullptr;
		}

		std::string clazz_name = ret->monadoViewClass.getName();
		if (clazz_name !=
		    "org.freedesktop.monado.auxiliary.MonadoView") {
			U_LOG_E("Unexpected class name: %s",
			        clazz_name.c_str());
			return nullptr;
		}

		if (ret->monadoViewClass.isNull()) {
			U_LOG_E("MonadoView was null.");
			return nullptr;
		}

		attachToActivity = ret->monadoViewClass.getStaticMethod(
		    "attachToActivity",
		    "(Landroid/app/Activity;)Lorg/freedesktop/monado/auxiliary/"
		    "MonadoView;");
		jni::method_t attachToActivity =
		    ret->monadoViewClass.getStaticMethod(
		        "attachToActivity", "Landroid.app.Activity");
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
android_custom_surface_get_surface(
    struct android_custom_surface *custom_surface)
{
	auto holder =
	    custom_surface->monadoView.get<jni::Object>("currentSurfaceHolder");
	if (holder.isNull()) {
		return nullptr;
	}
	SurfaceHolder surfaceHolder(holder);
	auto surf = surfaceHolder.getSurface();
	if (surf.isNull()) {
		return nullptr;
	}
	return ANativeWindow_fromSurface(jni::env(), surf.object().getHandle());
}
