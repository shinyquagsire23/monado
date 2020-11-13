// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simple Application subclass.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */
package org.freedesktop.monado.openxr_runtime;

import android.app.Application;

import dagger.hilt.android.HiltAndroidApp;

/**
 * Subclass required for Hilt usage.
 */
@HiltAndroidApp
public class MonadoOpenXrApplication extends Application {
}
