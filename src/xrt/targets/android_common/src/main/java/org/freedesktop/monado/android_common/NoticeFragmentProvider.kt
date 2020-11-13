// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface for target-specific .
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

package org.freedesktop.monado.android_common

import androidx.fragment.app.Fragment

/**
 * Provides a fragment with open-source license notices and attribution that we can put in our
 * "About" activity.
 *
 * Provided by dependency injection from the final target: e.g. AboutLibraries must come from there
 * to be sure to collect all dependencies to credit.
 */
interface NoticeFragmentProvider {

    fun makeNoticeFragment(): Fragment
}
