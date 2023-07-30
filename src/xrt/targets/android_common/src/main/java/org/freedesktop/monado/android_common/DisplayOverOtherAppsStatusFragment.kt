// Copyright 2021, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Fragment to display the Display Over Other Apps status and actions.
 * @author Jarvis Huang
 */
package org.freedesktop.monado.android_common

import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Process
import android.provider.Settings
import android.text.Html
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.Fragment
import dagger.hilt.android.AndroidEntryPoint

@AndroidEntryPoint
class DisplayOverOtherAppsStatusFragment : Fragment() {
    private var displayOverOtherAppsEnabled = false

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View? {
        val view =
            inflater.inflate(R.layout.fragment_display_over_other_app_status, container, false)
        updateStatus(view)
        view.findViewById<View>(R.id.btnLaunchDisplayOverOtherAppsSettings).setOnClickListener {
            launchDisplayOverOtherAppsSettings()
        }
        return view
    }

    private fun updateStatus(view: View?) {
        displayOverOtherAppsEnabled = Settings.canDrawOverlays(requireContext())
        val tv = view!!.findViewById<TextView>(R.id.textDisplayOverOtherAppsStatus)
        // Combining format with html style tag might have problem. See
        // https://developer.android.com/guide/topics/resources/string-resource.html#StylingWithHTML
        val msg =
            getString(
                R.string.msg_display_over_other_apps,
                if (displayOverOtherAppsEnabled) getString(R.string.enabled)
                else getString(R.string.disabled)
            )
        tv.text = Html.fromHtml(msg, Html.FROM_HTML_MODE_LEGACY)
    }

    private fun launchDisplayOverOtherAppsSettings() {
        // Since Android 11, framework ignores the uri and takes user to the top-level settings.
        // See https://developer.android.com/about/versions/11/privacy/permissions#system-alert
        // for detail.
        val intent =
            Intent(
                Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:" + context!!.packageName)
            )
        startActivityForResult(intent, REQUEST_CODE_DISPLAY_OVER_OTHER_APPS)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        // resultCode is always Activity.RESULT_CANCELED
        if (requestCode != REQUEST_CODE_DISPLAY_OVER_OTHER_APPS) {
            return
        }

        if (
            isRuntimeServiceRunning &&
                displayOverOtherAppsEnabled != Settings.canDrawOverlays(requireContext())
        ) {
            showRestartDialog()
        } else {
            updateStatus(view)
        }
    }

    @Suppress("DEPRECATION")
    private val isRuntimeServiceRunning: Boolean
        get() {
            var running = false
            val am = requireContext().getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
            for (service in am.getRunningServices(Int.MAX_VALUE)) {
                if (service.pid == Process.myPid()) {
                    running = true
                    break
                }
            }
            return running
        }

    private fun showRestartDialog() {
        val dialog: DialogFragment =
            RestartRuntimeDialogFragment.newInstance(
                getString(R.string.msg_display_over_other_apps_changed)
            )
        dialog.show(parentFragmentManager, null)
    }

    companion object {
        private const val REQUEST_CODE_DISPLAY_OVER_OTHER_APPS = 1000
    }
}
