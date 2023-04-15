// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple Application subclass.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */
package org.freedesktop.monado.openxr_runtime;

import android.app.Application;
import android.content.Context;

import androidx.annotation.NonNull;

import dagger.hilt.android.HiltAndroidApp;

/** Subclass required for Hilt usage. */
@HiltAndroidApp
public class MonadoOpenXrApplication extends Application {

    @Override
    public void onCreate() {
        super.onCreate();

        if (!BuildConfig.inProcess) {
            System.loadLibrary("monado-service");
            nativeStoreContext(getApplicationContext());
        }
    }

    private native void nativeStoreContext(@NonNull Context context);
}
