// Copyright 2021, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Class that manages surface
 * @author Jarvis Huang
 * @ingroup ipc_android
 */
package org.freedesktop.monado.ipc

import android.content.Context
import android.hardware.display.DisplayManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.util.Log
import android.view.Display
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import androidx.annotation.UiThread
import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.Condition
import java.util.concurrent.locks.ReentrantLock

/** Class that creates/manages surface on display. */
class SurfaceManager(context: Context) : SurfaceHolder.Callback {
    private val appContext: Context = context.applicationContext
    private val surfaceLock: ReentrantLock = ReentrantLock()
    private val surfaceCondition: Condition = surfaceLock.newCondition()
    private var callback: SurfaceHolder.Callback? = null
    private val uiHandler: Handler = Handler(Looper.getMainLooper())
    private val viewHelper: ViewHelper = ViewHelper(this)

    override fun surfaceCreated(holder: SurfaceHolder) {
        Log.i(TAG, "surfaceCreated")
        callback?.surfaceCreated(holder)

        surfaceLock.lock()
        surfaceCondition.signal()
        surfaceLock.unlock()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Log.i(TAG, "surfaceChanged, size: " + width + "x" + height)
        callback?.surfaceChanged(holder, format, width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        Log.i(TAG, "surfaceDestroyed")
        callback?.surfaceDestroyed(holder)
    }

    /**
     * Register a callback for surface status.
     *
     * @param callback Callback to be invoked.
     */
    fun setCallback(callback: SurfaceHolder.Callback?) {
        this.callback = callback
    }

    /**
     * Create surface on required display.
     *
     * @param displayId Target display id.
     * @param focusable True if the surface should be focusable; otherwise false.
     * @return True if operation succeeded.
     */
    @Synchronized
    fun createSurfaceOnDisplay(displayId: Int, focusable: Boolean): Boolean {
        if (!canDrawOverlays()) {
            Log.w(TAG, "Unable to draw over other apps!")
            return false
        }

        val dm = appContext.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        val targetDisplay = dm.getDisplay(displayId)
        if (targetDisplay == null) {
            Log.w(TAG, "Can't find target display, id: $displayId")
            return false
        }

        if (viewHelper.hasSamePropertiesWithCurrentView(targetDisplay, focusable)) {
            Log.i(TAG, "Reuse current surface")
            return true
        }

        if (Looper.getMainLooper().isCurrentThread) {
            viewHelper.removeAndAddView(appContext, targetDisplay, focusable)
        } else {
            uiHandler.post { viewHelper.removeAndAddView(appContext, targetDisplay, focusable) }
            surfaceLock.lock()
            try {
                surfaceCondition.await(1, TimeUnit.SECONDS)
            } catch (exception: InterruptedException) {
                exception.printStackTrace()
            } finally {
                Log.i(TAG, "surface ready")
                surfaceLock.unlock()
            }
        }
        return true
    }

    /**
     * Check if current process has the capability to draw over other applications.
     *
     * Implementation of [Settings.canDrawOverlays] checks both context and UID, therefore this
     * cannot be done in client side.
     *
     * @return True if current process can draw over other applications; otherwise false.
     */
    fun canDrawOverlays(): Boolean {
        return Settings.canDrawOverlays(appContext)
    }

    /** Destroy created surface. */
    fun destroySurface() {
        viewHelper.removeView()
    }

    /** Helper class that manages surface view. */
    private class ViewHelper(private val callback: SurfaceHolder.Callback) {
        private var view: SurfaceView? = null
        private var displayContext: Context? = null

        @UiThread
        fun removeAndAddView(context: Context, targetDisplay: Display, focusable: Boolean) {
            removeView()
            addView(context, targetDisplay, focusable)
        }

        @UiThread
        fun addView(context: Context, display: Display, focusable: Boolean) {
            // WindowManager is associated with display context.
            Log.i(TAG, "Add view to display " + display.displayId)
            displayContext = context.createDisplayContext(display)
            addViewInternal(displayContext!!, focusable)
        }

        @UiThread
        fun removeView() {
            if (view != null && displayContext != null) {
                removeViewInternal(displayContext!!)
                displayContext = null
            }
        }

        fun hasSamePropertiesWithCurrentView(display: Display, focusable: Boolean): Boolean {
            return if (view == null || displayContext == null) {
                false
            } else {
                isSameDisplay(displayContext!!, display) && !isFocusableChanged(focusable)
            }
        }

        /** Check whether given display is the one being used right now. */
        @Suppress("DEPRECATION")
        private fun isSameDisplay(context: Context, display: Display): Boolean {
            val wm = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
            return wm.defaultDisplay != null && wm.defaultDisplay.displayId == display.displayId
        }

        private fun isFocusableChanged(focusable: Boolean): Boolean {
            val lp = view!!.layoutParams as WindowManager.LayoutParams
            val currentFocusable = lp.flags == VIEW_FLAG_FOCUSABLE
            return focusable != currentFocusable
        }

        @UiThread
        private fun addViewInternal(context: Context, focusable: Boolean) {
            val v = SurfaceView(context)
            v.holder.addCallback(callback)
            val lp = WindowManager.LayoutParams()
            lp.type = WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
            lp.flags = if (focusable) VIEW_FLAG_FOCUSABLE else VIEW_FLAG_NOT_FOCUSABLE
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                lp.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            }

            val wm = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
            wm.addView(v, lp)
            if (focusable) {
                v.requestFocus()
            }

            view = v
        }

        @UiThread
        private fun removeViewInternal(context: Context) {
            val wm = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
            wm.removeView(view)
            view = null
        }

        companion object {
            @Suppress("DEPRECATION")
            private const val VIEW_FLAG_FOCUSABLE = WindowManager.LayoutParams.FLAG_FULLSCREEN

            @Suppress("DEPRECATION")
            private const val VIEW_FLAG_NOT_FOCUSABLE =
                WindowManager.LayoutParams.FLAG_FULLSCREEN or
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
        }
    }

    companion object {
        private const val TAG = "SurfaceManager"
    }
}
