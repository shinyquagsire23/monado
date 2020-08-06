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
 * Creates the main native compositor.
 *
 * @ingroup xrt_iface
 * @relates xrt_compositor_native
 */
struct xrt_compositor_native *
xrt_gfx_provider_create_native(struct xrt_device *xdev);


#ifdef __cplusplus
}
#endif
