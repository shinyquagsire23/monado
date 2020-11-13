// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple implementation of UiProvider.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

package org.freedesktop.monado.openxr_runtime

import android.app.PendingIntent
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.drawable.Icon
import dagger.hilt.android.qualifiers.ApplicationContext
import org.freedesktop.monado.android_common.AboutActivity
import org.freedesktop.monado.auxiliary.UiProvider
import javax.inject.Inject

class MonadoOpenXrUiProvider @Inject constructor(@ApplicationContext val context: Context) : UiProvider {

    /**
     * Gets a drawable for use in a notification, for the runtime/Monado-incorporating target.
     */
    override fun getNotificationIcon(): Icon? =
            Icon.createWithResource(context, R.drawable.ic_notif_xr_letters_custom)

    /**
     * Make a {@code PendingIntent} to launch an "About" activity for the runtime/target.
     */
    override fun makeAboutActivityPendingIntent(): PendingIntent =
            PendingIntent.getActivity(context,
                    0,
                    Intent.makeMainActivity(
                            ComponentName.createRelative(context,
                                    AboutActivity::class.qualifiedName!!)),
                    0
            )


}
