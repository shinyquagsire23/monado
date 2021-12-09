// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface used by the IPC service to create a notification to keep it in the foreground..
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

package org.freedesktop.monado.auxiliary

import android.app.Notification
import android.app.PendingIntent
import android.content.Context

/**
 * Interface for handling a foreground service notification.
 *
 * Exists so that the service itself doesn't have to deal with UI details.
 */
interface IServiceNotification {

    /**
     * Create and return a notification (creating the channel if applicable) that can be used in
     * {@code Service#startForeground()}
     */
    fun buildNotification(context: Context, pendingShutdownIntent: PendingIntent): Notification

    /**
     * Return the notification ID to use
     */
    fun getNotificationId(): Int
}
