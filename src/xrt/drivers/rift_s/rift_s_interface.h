// Copyright 2020-2021, Collabora, Ltd.
// Copyright 2022 Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to rift_s driver.
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */

#pragma once

#include "xrt/xrt_prober.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_rift_s Oculus Rift S driver
 * @ingroup drv
 *
 * @brief Driver for the Oculus Rift S and touch controllers
 *
 */

#define OCULUS_VR_INC_VID 0x2833
#define OCULUS_RIFT_S_PID 0x0051

/*!
 * Builder setup for Oculus Rift S HMD.
 *
 * @ingroup drv_rift_s
 */
struct xrt_builder *
rift_s_builder_create(void);

/*!
 * @dir drivers/rift_s
 *
 * @brief @ref drv_rift_s files.
 */

#ifdef __cplusplus
}
#endif
