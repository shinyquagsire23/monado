// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to @ref drv_vive.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include <stdlib.h>

#include "xrt/xrt_prober.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup drv_vive HTC Vive and Valve Index driver
 * @ingroup drv
 *
 * @brief Driver for the HTC Vive and Valve Index family of HMDs.
 */

#define HTC_VID 0x0bb4
#define VALVE_VID 0x28de

#define VIVE_PID 0x2c87
#define VIVE_LIGHTHOUSE_FPGA_RX 0x2000

#define VIVE_PRO_MAINBOARD_PID 0x0309
#define VIVE_PRO_LHR_PID 0x2300

#define VIVE_WATCHMAN_DONGLE 0x2101
#define VIVE_WATCHMAN_DONGLE_GEN2 0x2102


/*!
 * Probing function for Vive devices.
 *
 * @ingroup drv_vive
 */
int
vive_found(struct xrt_prober *xp,
           struct xrt_prober_device **devices,
           size_t num_devices,
           size_t index,
           cJSON *attached_data,
           struct xrt_device **out_xdev);


/*!
 * Probing function for HTC Vive and Valve Index controllers.
 *
 * @ingroup drv_vive
 */
int
vive_controller_found(struct xrt_prober *xp,
                      struct xrt_prober_device **devices,
                      size_t num_devices,
                      size_t index,
                      cJSON *attached_data,
                      struct xrt_device **out_xdevs);

/*!
 * @dir drivers/vive
 *
 * @brief @ref drv_vive files.
 */


#ifdef __cplusplus
}
#endif
