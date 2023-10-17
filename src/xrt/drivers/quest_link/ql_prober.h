// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to @ref drv_quest_link.
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#pragma once

#include <stdlib.h>

#include "xrt/xrt_prober.h"

#include "ql_hmd.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Probing function for Quest Link devices.
 *
 * @ingroup drv_quest_link
 * @see xrt_prober_found_func_t
 */
int
ql_found(struct xrt_prober *xp,
          struct xrt_prober_device **devices,
          size_t device_count,
          size_t index,
          cJSON *attached_data,
          struct xrt_device **out_xdev);

/*!
 * @dir drivers/quest_link
 *
 * @brief @ref drv_quest_link files.
 */


#ifdef __cplusplus
}
#endif
