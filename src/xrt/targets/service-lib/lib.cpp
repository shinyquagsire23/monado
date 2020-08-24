// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Library exposing IPC server.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */

#include "target_lists.h"

#include "jnipp.h"
#include "jni.h"

#include "wrap/android.os.h"
#include "wrap/android.view.h"

#include <android/native_window.h>
#include <android/native_window_jni.h>

using wrap::android::os::ParcelFileDescriptor;
using wrap::android::view::Surface;

extern "C" void
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeAddClient(
    JNIEnv *env, jobject thiz, jobject parcel_file_descriptor)
{
	jni::init(env);
	//! @todo do something!
	// This may be the "entry point" of the native code, or we could already
	// have another client running, etc.

	ParcelFileDescriptor pfd(parcel_file_descriptor);
	jni::Object monadoImpl(thiz);
}

extern "C" void
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeAppSurface(JNIEnv *env,
                                                            jobject thiz,
                                                            jobject surface)
{
	jni::init(env);
	Surface surf(surface);
	jni::Object monadoImpl(thiz);
	ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);

	//! @todo do something!
}
