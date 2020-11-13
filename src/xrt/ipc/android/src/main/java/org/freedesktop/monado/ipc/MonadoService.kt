// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Service implementation for exposing IMonado.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */
package org.freedesktop.monado.ipc

import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.util.Log
import dagger.hilt.android.AndroidEntryPoint
import org.freedesktop.monado.auxiliary.IServiceNotification
import javax.inject.Inject

/**
 * Implementation of a Service that provides the Monado AIDL interface.
 *
 * This is needed so that the APK can expose the binder service implemented in MonadoImpl.
 */
@AndroidEntryPoint
class MonadoService : Service() {
    private val binder = MonadoImpl()

    @Inject
    lateinit var serviceNotification: IServiceNotification

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        Log.d(TAG, "onStartCommand")
        // if this isn't a restart
        if (intent != null) {
            when (intent.action) {
                BuildConfig.SERVICE_ACTION -> handleStart()
                BuildConfig.SHUTDOWN_ACTION -> handleShutdown()
            }
        }
        return START_STICKY
    }

    private fun handleShutdown() {
        stopForeground(true)
        stopSelf()
    }

    override fun onBind(intent: Intent): IBinder? {
        return binder
    }

    private fun handleStart() {
        val pendingShutdownIntent = PendingIntent.getForegroundService(this,
                0,
                Intent(BuildConfig.SHUTDOWN_ACTION).setPackage(packageName),
                0)

        val notification = serviceNotification.buildNotification(this, pendingShutdownIntent)


        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(serviceNotification.getNotificationId(),
                    notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MANIFEST)
        } else {
            startForeground(serviceNotification.getNotificationId(),
                    notification)
        }

    }

    companion object {
        private const val TAG = "MonadoService"
    }
}
