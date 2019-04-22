// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common interface to probe for devices.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_device.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * A simple prober to probe for a HMD device connected to the system.
 *
 * @ingroup xrt_iface
 */
struct xrt_auto_prober
{
	struct xrt_device *(*lelo_dallas_autoprobe)(
	    struct xrt_auto_prober *xdev);
	void (*destroy)(struct xrt_auto_prober *xdev);
};

/*!
 * Call this function to create the @ref xrt_auto_prober. This function is setup
 * in the the very small target wrapper.c for each binary.
 *
 * @ingroup xrt_iface
 */
struct xrt_auto_prober *
xrt_auto_prober_create();


#ifdef __cplusplus
}
#endif
