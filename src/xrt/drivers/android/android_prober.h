// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to Android sensors driver.
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_android
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_android Android sensors driver
 * @ingroup drv
 *
 * @brief Generic driver for phone sensors.
 */

/*!
 * Probing function for Android sensors.
 *
 * @ingroup drv_android
 */
struct xrt_auto_prober *
android_create_auto_prober();


/*!
 * @dir drivers/android
 *
 * @brief @ref drv_android files.
 */


#ifdef __cplusplus
}
#endif
