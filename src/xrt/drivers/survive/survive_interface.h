// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to Libsurvive adapter.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_survive
 */

#pragma once


#include "xrt/xrt_results.h"
#include "xrt/xrt_defines.h"
#include "vive/vive_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_survive Lighthouse tracking using libsurvive
 * @ingroup drv
 *
 * @brief
 */

int
survive_get_devices(struct xrt_device **out_xdevs, struct vive_config **out_vive_config);

#ifdef __cplusplus
}
#endif
