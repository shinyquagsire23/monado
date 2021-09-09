// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface for @ref drv_euroc
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup drv_euroc
 */

#pragma once

#include "xrt/xrt_frameserver.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @defgroup drv_euroc Euroc driver
 * @ingroup drv
 *
 * @brief Provide euroc dataset playback features for SLAM evaluation
 *
 * This driver works with any dataset using the EuRoC format.
 * Original EuRoC datasets and more information about them can be found in
 * https://projects.asl.ethz.ch/datasets/doku.php?id=kmavvisualinertialdatasets
 */

/*!
 * Create an euroc player from a path to a dataset.
 *
 * @ingroup drv_euroc
 */
struct xrt_fs *
euroc_player_create(struct xrt_frame_context *xfctx, const char *path);

/*!
 * Create a auto prober for the fake euroc device.
 *
 * @ingroup drv_euroc
 */
struct xrt_auto_prober *
euroc_create_auto_prober(void);

/*!
 * @dir drivers/euroc
 *
 * @brief @ref drv_euroc files.
 */

#ifdef __cplusplus
}
#endif
