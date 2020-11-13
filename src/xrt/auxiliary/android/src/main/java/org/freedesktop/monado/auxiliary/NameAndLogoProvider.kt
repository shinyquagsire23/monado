// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface for target-specific branding things on Android.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

package org.freedesktop.monado.auxiliary

import android.graphics.drawable.Drawable

/**
 * Branding-related UI stuff. This interface must be provided by any Android "XRT Target".
 *
 * Intended for use in dependency injection.
 */
interface NameAndLogoProvider {
    /**
     * Gets a localized runtime name string for the runtime/Monado-incorporating target.
     */
    fun getLocalizedRuntimeName(): CharSequence

    /**
     * Gets a drawable for use in the about activity and elsewhere, for the runtime/Monado-incorporating target.
     */
    fun getLogoDrawable(): Drawable?
}
