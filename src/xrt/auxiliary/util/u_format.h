// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Format helpers and block code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_defines.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Return string for this format.
 *
 * @ingroup aux_util
 */
const char *
u_format_str(enum xrt_format f);

/*!
 * Is this format block based, also returns true for formats that 1x1 blocks.
 *
 * @ingroup aux_util
 */
bool
u_format_is_blocks(enum xrt_format f);

/*!
 * Returns the width of the block for the given format.
 *
 * @ingroup aux_util
 */
uint32_t
u_format_block_width(enum xrt_format f);

/*!
 * Returns the height of the block for the given format.
 *
 * @ingroup aux_util
 */
uint32_t
u_format_block_height(enum xrt_format f);

/*!
 * Returns the size of the block for the given format.
 *
 * @ingroup aux_util
 */
size_t
u_format_block_size(enum xrt_format f);

/*!
 * Calculate stride and size for the format and given width and height.
 *
 * @ingroup aux_util
 */
void
u_format_size_for_dimensions(enum xrt_format f, uint32_t width, uint32_t height, size_t *out_stride, size_t *out_size);


#ifdef __cplusplus
}
#endif
