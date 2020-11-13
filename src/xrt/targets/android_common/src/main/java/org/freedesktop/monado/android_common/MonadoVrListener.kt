// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation of VrListenerService.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

package org.freedesktop.monado.android_common

import android.content.ComponentName
import android.os.Build
import android.service.vr.VrListenerService
import android.util.Log
import android.widget.Toast
import androidx.annotation.RequiresApi

@RequiresApi(Build.VERSION_CODES.N)
class MonadoVrListener : VrListenerService() {
    // Would like to override
    // void onCurrentVrActivityChanged(
    //            ComponentName component, boolean running2dInVr, int pid)
    // as recommended, but not possible?
    override fun onCurrentVrActivityChanged(component: ComponentName?) {
        if (component == null) {
            Toast.makeText(this, "Now in VR for 2D", Toast.LENGTH_SHORT).show()
            Log.i("OpenXRListener", "Got VR mode for 2D")

        } else {
            Toast.makeText(this, "Now in VR for $component", Toast.LENGTH_SHORT).show()
            Log.i("OpenXRListener", "Got VR mode for component: $component")
        }

    }
}
