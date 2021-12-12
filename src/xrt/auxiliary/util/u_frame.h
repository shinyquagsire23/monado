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

/*!
 * Clones a frame. The cloned frame is not freed when the original frame is freed; instead the cloned frame is freed
 * when its reference reaches zero.
 */
void
u_frame_clone(struct xrt_frame *to_copy, struct xrt_frame **out_frame);

/*!
 * Creates a frame out of a region of interest from @p original frame. Does not
 * duplicate data, increases @p original refcount instead.
 */
void
u_frame_create_roi(struct xrt_frame *original, struct xrt_rect roi, struct xrt_frame **out_frame);

#ifdef __cplusplus
}
#endif
