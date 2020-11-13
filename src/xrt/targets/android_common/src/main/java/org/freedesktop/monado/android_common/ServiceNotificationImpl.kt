// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of service notification.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */
package org.freedesktop.monado.android_common

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.graphics.drawable.Icon
import android.os.Build
import androidx.core.app.NotificationManagerCompat
import org.freedesktop.monado.auxiliary.IServiceNotification
import org.freedesktop.monado.auxiliary.NameAndLogoProvider
import org.freedesktop.monado.auxiliary.UiProvider
import javax.inject.Inject

class ServiceNotificationImpl @Inject constructor() : IServiceNotification {
    companion object {
        private const val CHANNEL_ID = "org.freedesktop.monado.ipc.CHANNEL"

        // guaranteed random by fair die roll ;)
        private const val NOTIFICATION_ID = 8562
    }

    @Inject
    lateinit var uiProvider: UiProvider

    @Inject
    lateinit var nameAndLogoProvider: NameAndLogoProvider

    /**
     * Start creating a Notification.Builder in a version-compatible way, including our
     * notification channel if applicable.
     */
    private fun makeNotificationBuilder(context: Context): Notification.Builder {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(context, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(context)
        }
    }

    private fun createChannel(context: Context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {

            val notificationChannel = NotificationChannel(CHANNEL_ID,
                    nameAndLogoProvider.getLocalizedRuntimeName(),
                    NotificationManager.IMPORTANCE_LOW)
            notificationChannel.description = context.getString(
                    R.string.channel_desc,
                    nameAndLogoProvider.getLocalizedRuntimeName())
            notificationChannel.setShowBadge(false)
            notificationChannel.enableLights(false)
            notificationChannel.enableVibration(false)

            NotificationManagerCompat.from(context)
                    .createNotificationChannel(notificationChannel)
        }

    }

    /**
     * Create and return a notification (creating the channel if applicable) that can be used in
     * {@code Service#startForeground()}
     */
    override fun buildNotification(context: Context, pendingShutdownIntent: PendingIntent): Notification {
        createChannel(context)

        val action = Notification.Action.Builder(
                Icon.createWithResource(context, R.drawable.ic_feathericons_x),
                context.getString(R.string.notifExitRuntime),
                pendingShutdownIntent)
                .build()
        // Make a notification for our foreground service
        // When selected it will open the "About" activity
        val builder = makeNotificationBuilder(context)
                .setOngoing(true)
                .setContentTitle(nameAndLogoProvider.getLocalizedRuntimeName())
                .setContentText(context.getString(
                        R.string.notif_text,
                        nameAndLogoProvider.getLocalizedRuntimeName()))
                .setShowWhen(false)
                .addAction(action)
                .setContentIntent(uiProvider.makeAboutActivityPendingIntent())

        // Notification icon is optional
        uiProvider.getNotificationIcon()?.let { icon: Icon ->
            builder.setSmallIcon(icon)
        }

        // Configure intent is optional
        uiProvider.makeConfigureActivityPendingIntent()?.let { pendingIntent: PendingIntent ->

            val configureAction = Notification.Action.Builder(
                    Icon.createWithResource(context, R.drawable.ic_feathericons_settings),
                    context.getString(R.string.notifConfigure),
                    pendingIntent)
                    .build()
            builder.addAction(configureAction)
        }
        return builder.build()
    }

    /**
     * Return the notification ID to use
     */
    override fun getNotificationId(): Int = NOTIFICATION_ID
}
