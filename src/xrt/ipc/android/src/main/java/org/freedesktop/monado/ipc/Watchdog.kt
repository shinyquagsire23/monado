// Copyright 2021, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Monitor client connections.
 * @author Jarvis Huang
 * @ingroup ipc_android
 */
package org.freedesktop.monado.ipc

import android.os.Handler
import android.os.HandlerThread
import android.os.Message
import java.util.concurrent.atomic.AtomicInteger

/** Client watchdog, to determine whether runtime service should be stopped. */
class Watchdog(
    private val shutdownDelayMilliseconds: Long,
    private val shutdownListener: ShutdownListener
) {
    /**
     * Interface definition for callbacks to be invoked when there's no client connected. Noted that
     * all the callbacks run on background thread.
     */
    interface ShutdownListener {
        /** Callback to be invoked when last client disconnected. */
        fun onPrepareShutdown()

        /** Callback to be invoked when shutdown delay ended and there's no new client connected. */
        fun onShutdown()
    }

    private val clientCount = AtomicInteger(0)

    private lateinit var shutdownHandler: Handler

    private lateinit var shutdownThread: HandlerThread

    fun startMonitor() {
        shutdownThread = HandlerThread("monado-client-watchdog")
        shutdownThread.start()
        shutdownHandler =
            object : Handler(shutdownThread.looper) {
                override fun handleMessage(msg: Message) {
                    when (msg.what) {
                        MSG_SHUTDOWN ->
                            if (clientCount.get() == 0) {
                                shutdownListener.onShutdown()
                            }
                    }
                }
            }
    }

    fun stopMonitor() {
        shutdownThread.quitSafely()
    }

    fun onClientConnected() {
        clientCount.incrementAndGet()
        shutdownHandler.removeMessages(MSG_SHUTDOWN)
    }

    fun onClientDisconnected() {
        if (clientCount.decrementAndGet() == 0) {
            shutdownListener.onPrepareShutdown()
            shutdownHandler.sendEmptyMessageDelayed(MSG_SHUTDOWN, shutdownDelayMilliseconds)
        }
    }

    companion object {
        private const val MSG_SHUTDOWN = 1000
    }
}
