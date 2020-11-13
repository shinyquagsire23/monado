// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface for target-specific UI-related things on Android.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */
package org.freedesktop.monado.auxiliary

import android.app.PendingIntent
import android.content.Context
import android.graphics.drawable.Drawable
import android.graphics.drawable.Icon

/**
 * Non-branding-related UI stuff. This interface must be provided by any Android "XRT Target".
 *
 * Intended for use in dependency injection.
 */
interface UiProvider {
    /**
     * Gets a drawable for use in a notification, for the runtime/Monado-incorporating target.
     *
     * Optional - you can return null.
     */
    fun getNotificationIcon(): Icon? = null

    /**
     * Make a {@code PendingIntent} to launch an "About" activity for the runtime/target.
     */
    fun makeAboutActivityPendingIntent(): PendingIntent

    /**
     * Make a {@code PendingIntent} to launch a configuration activity, if provided by the target.
     */
    fun makeConfigureActivityPendingIntent(): PendingIntent? = null

}
