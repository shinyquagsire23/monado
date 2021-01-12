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
 * Creates the main system compositor.
 *
 * @ingroup xrt_iface
 * @relates xrt_system_compositor
 */
xrt_result_t
xrt_gfx_provider_create_system(struct xrt_device *xdev, struct xrt_system_compositor **out_xsysc);


#ifdef __cplusplus
}
#endif
