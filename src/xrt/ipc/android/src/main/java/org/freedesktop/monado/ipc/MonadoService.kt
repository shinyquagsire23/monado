// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Service implementation for exposing IMonado.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */
package org.freedesktop.monado.ipc

import android.app.Service
import android.content.Intent
import android.os.IBinder

/**
 * Minimal implementation of a Service.
 *
 * This is needed so that the APK can expose the binder service implemented in MonadoImpl.
 */
class MonadoService : Service() {
    val monado: MonadoImpl = MonadoImpl()

    override fun onBind(intent: Intent): IBinder? {
        return monado;
    }
}
