// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helper providing a shutdown button in the about activity.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

package org.freedesktop.monado.android_common

import android.os.Process
import android.view.View
import android.widget.Button

class ShutdownProcess {
    companion object {
        /**
         * Show and handle the shutdown runtime button.
         */
        fun setupRuntimeShutdownButton(activity: AboutActivity) {
            val button =
                    activity.findViewById<Button>(R.id.shutdown)
            button.visibility = View.VISIBLE
            button.setOnClickListener {
                Process.killProcess(Process.myPid())
            }
        }
    }
}
