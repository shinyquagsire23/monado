// Copyright 2019-2020, Collabora, Ltd.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to North Star driver code.
 * @author Nova King <technobaboo@gmail.com>
 * @ingroup drv_ns
 */

#pragma once
#include "util/u_json.h"
#include "xrt/xrt_device.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup drv_ns North Star driver
 * @ingroup drv
 *
 * @brief Driver for the North Star HMD.
 */

/*!
 * Creates a North Star HMD.
 *
 * @ingroup drv_ns
 */

struct xrt_device *
ns_hmd_create(const cJSON *config_json);

/*!
 * @dir drivers/north_star
 *
 * @brief @ref drv_ns files.
 */


#ifdef __cplusplus
}
#endif
