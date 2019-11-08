// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to @ref drv_hydra
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup drv_hydra
 */

#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
/*!
 * @defgroup drv_hydra Razer Hydra driver
 * @ingroup drv
 *
 * @brief Driver for the Razer Hydra motion controllers.
 */


#define HYDRA_VID 0x1532
#define HYDRA_PID 0x0300

/*!
 * Probing function for Razer Hydra devices.
 *
 * @ingroup drv_hydra
 */
int
hydra_found(struct xrt_prober *xp,
            struct xrt_prober_device **devices,
            size_t num_devices,
            size_t index,
            cJSON *attached_data,
            struct xrt_device **out_xdevs);


/*!
 * @dir drivers/hydra
 *
 * @brief @ref drv_hydra files.
 */

#ifdef __cplusplus
}
#endif
