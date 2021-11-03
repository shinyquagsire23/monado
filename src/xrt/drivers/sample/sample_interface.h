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
 * This device has an implementation of @ref xrt_auto_prober to perform hardware
 * detection, as well as an implementation of @ref xrt_device for the actual device.
 *
 * If your device is or has USB HID that **can** be detected based on USB VID/PID,
 * you can skip the @ref xrt_auto_prober implementation, and instead implement a
 * "found" function that matches the signature expected by xrt_prober_entry::found.
 * See for example @ref hdk_found.
 *
 * After you copy and rename these files, you can customize them with the following,
 * assuming your new device type is called `struct my_device` or `md` for short, and
 * your auto-prober is called `struct my_device_auto_prober` or `mdap` for short:
 *
 * ```sh
 * # First pattern is for renaming device types,
 * # second is for renaming device variables,
 * # third is for renaming device macros.
 * # Fourth and fifth are for renaming auto prober types and variables, respectively.
 * # The last two are for renaming the environment variable and function name
 * # for the environment variable logging config.
 * sed -r -e 's/sample_hmd/my_device/g' \
 *   -e 's/\bsh\b/md/g' \
 *   -e 's/sample_auto_prober/my_device_auto_prober/g' \
 *   -e 's/\bsap\b/mdap/g' \
 *   -e 's/\bSH_/MD_/g' \
 *   -e 's/sample/my_device/g' \
 *   -e 's/SAMPLE/MY_DEVICE/g' \
 *   -i *.c *.h
 * ```
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
