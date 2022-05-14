// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to @ref drv_psvr.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_psvr
 */

#pragma once

#include "xrt/xrt_compiler.h"


#ifdef __cplusplus
extern "C" {
#endif

struct xrt_tracked_psvr;


/*!
 * @defgroup drv_psvr PSVR driver
 * @ingroup drv
 *
 * @brief Driver for the Sony PSVR HMD.
 */

/*!
 * Vendor id for PSVR.
 *
 * @ingroup drv_psvr
 */
#define PSVR_VID 0x054c

/*!
 * Product id for PSVR.
 *
 * @ingroup drv_psvr
 */
#define PSVR_PID 0x09af

/*!
 * Create PSVR device, with a optional tracker.
 *
 * @ingroup drv_psvr
 */
struct xrt_device *
psvr_device_create(struct xrt_tracked_psvr *tracker);

/*!
 * Create a probe for PSVR devices.
 *
 * @ingroup drv_psvr
 * @relates xrt_auto_prober
 */
struct xrt_auto_prober *
psvr_create_auto_prober(void);

/*!
 * @dir drivers/psvr
 *
 * @brief @ref drv_psvr files.
 */


#ifdef __cplusplus
}
#endif
