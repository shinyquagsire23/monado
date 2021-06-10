// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface header for DepthAI camera.
 * @author Moses Turner <moses@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_depthai
 */

#pragma once

#include "xrt/xrt_frameserver.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @defgroup drv_depthai DepthAI frameserver driver
 * @ingroup drv
 *
 * @brief Frameserver for the DepthAI camera module.
 */

/*!
 * Create a DepthAI frameserver using a single RGB camera.
 *
 * @ingroup drv_depthai
 */
struct xrt_fs *
depthai_fs_single_rgb(struct xrt_frame_context *xfctx);


#ifdef __cplusplus
}
#endif
