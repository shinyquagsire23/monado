// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header for the main compositor interface.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup comp_main
 *
 * Formerly a header for defining a XRT graphics provider.
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Creates the main compositor, it doesn't return itself but instead wraps
 * itself with a system compositor. The main compositor is a native compositor.
 *
 * @ingroup comp_main
 * @relates xrt_system_compositor
 */
xrt_result_t
comp_main_create_system_compositor(struct xrt_device *xdev, struct xrt_system_compositor **out_xsysc);


#ifdef __cplusplus
}
#endif
