// Copyright 2020-2021, N Madsen.
// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to the WMR driver.
 * @author nima01 <nima_zero_one@protonmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_wmr
 */

#pragma once

#include "xrt/xrt_prober.h"

#include "wmr_common.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup drv_wmr Windows Mixed Reality driver
 * @ingroup drv
 *
 * @brief Windows Mixed Reality driver.
 */

/*!
 * @dir drivers/wmr
 *
 * @brief @ref drv_wmr files.
 */


/*
 *
 * Builder interface.
 *
 */

/*!
 * Results from searching for host attached Bluetooth controllers.
 *
 * @ingroup drv_wmr
 */
struct wmr_bt_controllers_search_results
{
	struct xrt_prober_device *left;
	struct xrt_prober_device *right;
};

/*!
 * Search for a left and right pair of Windows Mixed Reality controllers, groups
 * them by type (Classic/Odyssey/G2). Preferring Odyssey over Classic. Will mix
 * types in order to get a complete left and right pair if need be, but prefers
 * matching types first. G2 currently not supported.
 *
 * @ingroup drv_wmr
 */
void
wmr_find_bt_controller_pair(struct xrt_prober *xp,
                            struct xrt_prober_device **xpdevs,
                            size_t xpdev_count,
                            enum u_logging_level log_level,
                            struct wmr_bt_controllers_search_results *out_wbtcsr);

/*!
 * Results from searching for a companion device. Doctor?
 *
 * @ingroup drv_wmr
 */
struct wmr_companion_search_results
{
	struct xrt_prober_device *xpdev_companion;
	enum wmr_headset_type type;
};

/*!
 * Searches for the the list of xpdevs for the companion device of a holo lens
 * device.
 *
 * @ingroup drv_wmr
 */
void
wmr_find_companion_device(struct xrt_prober *xp,
                          struct xrt_prober_device **xpdevs,
                          size_t xpdev_count,
                          enum u_logging_level log_level,
                          struct xrt_prober_device *xpdev_holo,
                          struct wmr_companion_search_results *out_wcsr);

/*!
 * Results from searching for a headset.
 *
 * @ingroup drv_wmr
 */
struct wmr_headset_search_results
{
	struct xrt_prober_device *xpdev_holo;
	struct xrt_prober_device *xpdev_companion;
	enum wmr_headset_type type;
};

/*!
 * Find a headsets.
 *
 * @ingroup drv_wmr
 */
void
wmr_find_headset(struct xrt_prober *xp,
                 struct xrt_prober_device **xpdevs,
                 size_t xpdev_count,
                 enum u_logging_level log_level,
                 struct wmr_headset_search_results *out_whsr);


/*
 *
 * Creation extensions.
 *
 */

/*!
 * Creates a WMR headset with the given devices and of headset type.
 *
 * @ingroup drv_wmr
 */
xrt_result_t
wmr_create_headset(struct xrt_prober *xp,
                   struct xrt_prober_device *xpdev_holo,
                   struct xrt_prober_device *xpdev_companion,
                   enum wmr_headset_type type,
                   enum u_logging_level log_level,
                   struct xrt_device **out_hmd,
                   struct xrt_device **out_left,
                   struct xrt_device **out_right,
                   struct xrt_device **out_ht_left,
                   struct xrt_device **out_ht_right);

/*!
 * Creates a WMR BT controller device.
 *
 * @ingroup drv_wmr
 */
xrt_result_t
wmr_create_bt_controller(struct xrt_prober *xp,
                         struct xrt_prober_device *xpdev,
                         enum u_logging_level log_level,
                         struct xrt_device **out_xdev);


#ifdef __cplusplus
}
#endif
