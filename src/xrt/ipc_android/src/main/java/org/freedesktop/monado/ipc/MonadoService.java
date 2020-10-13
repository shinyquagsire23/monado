// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Service implementation for exposing IMonado.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */


package org.freedesktop.monado.ipc;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;

import androidx.annotation.Nullable;

/**
 * Minimal implementation of a Service.
 *
 * This is needed so that the APK can expose the binder service implemented in MonadoImpl.
 */
public class MonadoService extends Service {

    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return new MonadoImpl();
    }

}
