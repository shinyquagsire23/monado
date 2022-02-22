// Copyright 2021, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Class to handle system ui visibility
 * @author Jarvis Huang
 * @ingroup aux_android_java
 */
package org.freedesktop.monado.auxiliary

import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import androidx.annotation.RequiresApi

/** Helper class that handles system ui visibility. */
class SystemUiController(view: View) {
    private val impl: Impl =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsControllerImpl(view)
        } else {
            SystemUiVisibilityImpl(view)
        }

    /** Hide system ui and make fullscreen. */
    fun hide() {
        impl.hide()
    }

    private abstract class Impl(var view: View) {
        private val uiHandler: Handler = Handler(Looper.getMainLooper())
        abstract fun hide()
        fun runOnUiThread(runnable: Runnable) {
            uiHandler.post(runnable)
        }
    }

    @Suppress("DEPRECATION")
    private class SystemUiVisibilityImpl(view: View) : Impl(view) {
        override fun hide() {
            runOnUiThread { view.systemUiVisibility = FLAG_FULL_SCREEN_IMMERSIVE_STICKY }
        }

        companion object {
            private const val FLAG_FULL_SCREEN_IMMERSIVE_STICKY =
                // Give us a stable view of content insets
                (View.SYSTEM_UI_FLAG_LAYOUT_STABLE // Be able to do fullscreen and hide navigation
                or
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                    View.SYSTEM_UI_FLAG_FULLSCREEN or
                    View.SYSTEM_UI_FLAG_HIDE_NAVIGATION // we want sticky immersive
                    or
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
        }

        init {
            runOnUiThread {
                view.setOnSystemUiVisibilityChangeListener { visibility: Int ->
                    // If not fullscreen, fix it.
                    if (0 == visibility and View.SYSTEM_UI_FLAG_FULLSCREEN) {
                        hide()
                    }
                }
            }
        }
    }

    @RequiresApi(api = Build.VERSION_CODES.R)
    private class WindowInsetsControllerImpl(view: View) : Impl(view) {
        override fun hide() {
            runOnUiThread {
                val controller = view.windowInsetsController
                controller!!.hide(
                    WindowInsets.Type.displayCutout() or
                        WindowInsets.Type.statusBars() or
                        WindowInsets.Type.navigationBars()
                )
                controller.systemBarsBehavior =
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        }

        init {
            runOnUiThread {
                view.windowInsetsController?.addOnControllableInsetsChangedListener {
                    _: WindowInsetsController?,
                    typeMask: Int ->
                    if (
                        typeMask and WindowInsets.Type.displayCutout() == 1 ||
                            typeMask and WindowInsets.Type.statusBars() == 1 ||
                            typeMask and WindowInsets.Type.navigationBars() == 1
                    ) {
                        hide()
                    }
                }
            }
        }
    }
}
