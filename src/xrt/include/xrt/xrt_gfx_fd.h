// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining a XRT graphics provider.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"

#ifdef __cplusplus
extern "C" {
#endif


struct time_state;

/*!
 * Creates the main fd compositor.
 *
 * @ingroup xrt_iface
 */
struct xrt_compositor_fd *
xrt_gfx_provider_create_fd(struct xrt_device *xdev, bool flip_y);


#ifdef __cplusplus
}
#endif
