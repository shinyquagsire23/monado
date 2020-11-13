// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Module that binds all the dependencies we inject with Hilt.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

package org.freedesktop.monado.openxr_runtime

import dagger.Binds
import dagger.Module
import dagger.hilt.InstallIn
import dagger.hilt.android.components.ApplicationComponent
import org.freedesktop.monado.android_common.NoticeFragmentProvider
import org.freedesktop.monado.android_common.ServiceNotificationImpl
import org.freedesktop.monado.auxiliary.IServiceNotification
import org.freedesktop.monado.auxiliary.NameAndLogoProvider
import org.freedesktop.monado.auxiliary.UiProvider

/**
 * This is implemented by Hilt/Dagger to do dependency injection.
 *
 * Each declaration essentially signals to Hilt/Dagger what subclass/implementation of a
 * base/interface to use for each thing it must inject.
 */
@Module
@InstallIn(ApplicationComponent::class)
abstract class MonadoOpenXrAndroidModule {
    @Binds
    abstract fun bindUiProvider(uiProvider: MonadoOpenXrUiProvider): UiProvider

    @Binds
    abstract fun bindNameAndLogo(monadoOpenXrBrandingUiProvider: MonadoOpenXrBrandingUiProvider): NameAndLogoProvider

    @Binds
    abstract fun bindNoticeFragment(noticeFragmentProvider: AboutLibrariesNoticeFragmentProvider): NoticeFragmentProvider

    @Binds
    abstract fun bindServiceNotification(serviceNotificationImpl: ServiceNotificationImpl): IServiceNotification
}
