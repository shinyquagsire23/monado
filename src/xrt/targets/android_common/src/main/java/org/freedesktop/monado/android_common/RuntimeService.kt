// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple service for Android to just expose metadata.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

package org.freedesktop.monado.android_common

import android.app.Service
import android.content.Intent
import android.os.IBinder

class RuntimeService : Service() {
    override fun onBind(intent: Intent?): IBinder? {
        return null
    }
}
