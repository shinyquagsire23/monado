// Copyright 2021, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Fragment to display the reason of runtime restart.
 * @author Jarvis Huang
 */
package org.freedesktop.monado.android_common

import android.app.AlarmManager
import android.app.Dialog
import android.app.PendingIntent
import android.content.Context
import android.content.DialogInterface
import android.content.Intent
import android.os.Bundle
import android.os.Process
import androidx.appcompat.app.AlertDialog
import androidx.fragment.app.DialogFragment

class RestartRuntimeDialogFragment : DialogFragment() {

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val message = arguments!!.getString(ARGS_KEY_MESSAGE)
        val builder = AlertDialog.Builder(requireActivity())
        builder.setMessage(message)
            .setCancelable(false)
            .setPositiveButton(R.string.restart) { _: DialogInterface?, _: Int ->
                delayRestart(DELAY_RESTART_DURATION)
                //! @todo elegant way to stop service? A bounded service might be restarted by
                //        framework automatically.
                Process.killProcess(Process.myPid())
            }
        return builder.create()
    }

    private fun delayRestart(delayMillis: Long) {
        val intent = Intent(requireContext(), AboutActivity::class.java)
        val pendingIntent = PendingIntent.getActivity(
            requireContext(), REQUEST_CODE,
            intent, PendingIntent.FLAG_CANCEL_CURRENT
        )
        val am = requireContext().getSystemService(Context.ALARM_SERVICE) as AlarmManager
        am.setExact(AlarmManager.RTC, System.currentTimeMillis() + delayMillis, pendingIntent)
    }

    companion object {
        private const val ARGS_KEY_MESSAGE = "message"
        private const val REQUEST_CODE = 2000
        private const val DELAY_RESTART_DURATION: Long = 200

        @JvmStatic
        fun newInstance(msg: String): RestartRuntimeDialogFragment {
            val fragment = RestartRuntimeDialogFragment()
            val args = Bundle()
            args.putString(ARGS_KEY_MESSAGE, msg)
            fragment.arguments = args
            return fragment
        }
    }
}
