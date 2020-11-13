// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple implementation of NameAndLogoProvider.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

package org.freedesktop.monado.openxr_runtime

import android.content.Context
import android.graphics.drawable.Drawable
import androidx.core.content.ContextCompat
import dagger.hilt.android.qualifiers.ApplicationContext
import org.freedesktop.monado.auxiliary.NameAndLogoProvider
import javax.inject.Inject

class MonadoOpenXrBrandingUiProvider @Inject constructor(@ApplicationContext val context: Context) : NameAndLogoProvider {
    /**
     * Gets a localized runtime name string for the runtime/Monado-incorporating target.
     */
    override fun getLocalizedRuntimeName(): CharSequence = context.packageManager.getApplicationLabel(context.applicationInfo)

    /**
     * Gets a drawable for use in the about activity and elsewhere, for the runtime/Monado-incorporating target.
     */
    override fun getLogoDrawable(): Drawable? = ContextCompat.getDrawable(context, R.drawable.ic_monado_logo)
}
