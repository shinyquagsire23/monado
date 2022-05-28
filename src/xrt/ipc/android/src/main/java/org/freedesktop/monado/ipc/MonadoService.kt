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
class MonadoService : Service(), Watchdog.ShutdownListener {
    private lateinit var binder: MonadoImpl

    private lateinit var watchdog: Watchdog

    @Inject
    lateinit var serviceNotification: IServiceNotification

    private lateinit var surfaceManager: SurfaceManager

    override fun onCreate() {
        super.onCreate()

        surfaceManager = SurfaceManager(this)
        binder = MonadoImpl(surfaceManager)
        watchdog = Watchdog(
            // If the surface comes from client, just stop the service when client disconnected
            // because the surface belongs to the client.
            if (surfaceManager.canDrawOverlays()) BuildConfig.WATCHDOG_TIMEOUT_MILLISECONDS else 0,
            this
        )
        watchdog.startMonitor()

        // start the service so it could be foregrounded
        val intent = Intent(this, javaClass)
        intent.action = BuildConfig.SERVICE_ACTION
        startService(intent)
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.d(TAG, "onDestroy")

        binder.shutdown();
        watchdog.stopMonitor()
        surfaceManager.destroySurface()
    }

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

    override fun onBind(intent: Intent): IBinder? {
        Log.d(TAG, "onBind");
        watchdog.onClientConnected()
        return binder
    }

    private fun handleStart() {
        var flags = 0
        // From targeting S+, the PendingIntent needs one of FLAG_IMMUTABLE and FLAG_MUTABLE
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            flags = PendingIntent.FLAG_IMMUTABLE
        }
        val pendingShutdownIntent = PendingIntent.getForegroundService(
            this,
            0,
            Intent(BuildConfig.SHUTDOWN_ACTION).setPackage(packageName),
            flags
        )

        val notification = serviceNotification.buildNotification(this, pendingShutdownIntent)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                serviceNotification.getNotificationId(),
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MANIFEST
            )
        } else {
            startForeground(
                serviceNotification.getNotificationId(),
                notification
            )
        }
    }

    override fun onUnbind(intent: Intent?): Boolean {
        Log.d(TAG, "onUnbind");
        watchdog.onClientDisconnected()
        return true;
    }

    override fun onRebind(intent: Intent?) {
        Log.d(TAG, "onRebind");
        watchdog.onClientConnected()
    }

    override fun onPrepareShutdown() {
        Log.d(TAG, "onPrepareShutdown")
    }

    override fun onShutdown() {
        Log.d(TAG, "onShutdown")
        handleShutdown()
    }

    private fun handleShutdown() {
        stopForeground(true)
        stopSelf()
    }

    companion object {
        private const val TAG = "MonadoService"
    }
}
