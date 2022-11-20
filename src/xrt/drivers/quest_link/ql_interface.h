// Copyright 2020-2021, Collabora, Ltd.
// Copyright 2022 Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to quest_link driver.
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_quest_link
 */

#pragma once

#include "xrt/xrt_prober.h"

#include "ql_prober.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_quest_link Meta Quest Link driver
 * @ingroup drv
 *
 * @brief Driver for the Meta Quest Link and touch controllers
 *
 */

#define META_PLATFORMS_TECH_LLC_VID 0x2833
#define QUEST_SLEEPING_UMS_PID      0x0083
#define QUEST_XRSP_PID              0x0137
#define QUEST_MTP_XRSP_PID          0x0182
#define QUEST_MTP_XRSP_ADB_PID      0x0183
#define QUEST_XRSP_ADB_PID          0x0186

/*!
 * Builder setup for Meta Quest Link HMD.
 *
 * @ingroup drv_quest_link
 */
struct xrt_builder *
ql_builder_create(void);

/*!
 * @dir drivers/quest_link
 *
 * @brief @ref drv_quest_link files.
 */

#ifdef __cplusplus
}
#endif
