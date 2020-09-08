// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

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
	try {
		std::unique_ptr<android_custom_surface> ret =
		    std::make_unique<android_custom_surface>();

		ret->activity = Activity((jobject)activity);

		// Pass activity as the context because it's a subclass
		ret->monadoViewClass =
		    jni::Class((jclass)android_load_class_from_package(
		        vm, XRT_ANDROID_PACKAGE, activity,
		        "org.freedesktop.monado.auxiliary.MonadoView"));
		if (ret->monadoViewClass.isNull()) {
			return nullptr;
		}

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
