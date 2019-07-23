// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  @ref xrt_fs_sink converters and other helpers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_defines.h"
#include "xrt/xrt_frameserver.h"

#ifdef __cplusplus
extern "C" {
#endif


void
u_sink_create_format_converter(enum xrt_format f,
                               struct xrt_fs_sink *downstream,
                               struct xrt_fs_sink **out_xfs);


#ifdef __cplusplus
}
#endif
