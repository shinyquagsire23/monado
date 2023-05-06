// Copyright 2023, Collabora, Ltd.
// Copyright 2023, Jarett Millard
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to @ref drv_pssense
 * @author Jarett Millard <jarett.millard@gmail.com>
 * @ingroup drv_pssense
 */

#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
/*!
 * @defgroup drv_pssense PlayStation Sense driver
 * @ingroup drv
 *
 * @brief Driver for the PlayStation Sense motion controllers.
 */

#define PSSENSE_VID 0x054C
#define PSSENSE_PID_LEFT 0x0E45
#define PSSENSE_PID_RIGHT 0x0E46

/*!
 * Probing function for PlayStation Sense devices.
 *
 * @ingroup drv_pssense
 * @see xrt_prober_found_func_t
 */
int
pssense_found(struct xrt_prober *xp,
              struct xrt_prober_device **devices,
              size_t device_count,
              size_t index,
              cJSON *attached_data,
              struct xrt_device **out_xdevs);

/*!
 * @dir drivers/pssense
 *
 * @brief @ref drv_pssense files.
 */

#ifdef __cplusplus
}
#endif
