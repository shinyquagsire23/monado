// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  @ref xrt_frame helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_frame.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Creates a single non-pooled frame, when the reference reaches zero it is
 * freed.
 */
void
u_frame_create_one_off(enum xrt_format f, uint32_t width, uint32_t height, struct xrt_frame **out_frame);


#ifdef __cplusplus
}
#endif
