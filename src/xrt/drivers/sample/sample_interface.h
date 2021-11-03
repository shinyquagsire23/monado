// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to sample driver.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_sample
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_sample Sample driver
 * @ingroup drv
 *
 * @brief Simple do-nothing sample driver, that cannot be detected by USB VID/PID
 * and thus exposes an "auto-prober" to explicitly discover the device.
 *
 * See @ref writing-driver for additional information.
 *
 * This device has an implementation of @ref xrt_auto_prober to perform hardware
 * detection, as well as an implementation of @ref xrt_device for the actual device.
 *
 * If your device is or has USB HID that **can** be detected based on USB VID/PID,
 * you can skip the @ref xrt_auto_prober implementation, and instead implement a
 * "found" function that matches the signature expected by xrt_prober_entry::found.
 * See for example @ref hdk_found.
 */

/*!
 * Create a auto prober for a sample device.
 *
 * @ingroup drv_sample
 */
struct xrt_auto_prober *
sample_create_auto_prober(void);

/*!
 * Create a sample hmd.
 *
 * This is only exposed so that the prober (in one source file)
 * can call the construction function (in another.)
 * @ingroup drv_sample
 */
struct xrt_device *
sample_hmd_create(void);

/*!
 * @dir drivers/sample
 *
 * @brief @ref drv_sample files.
 */


#ifdef __cplusplus
}
#endif
