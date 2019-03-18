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
 * A prober for HMD devices connected to the system.
 *
 * @ingroup xrt_iface
 */
struct xrt_prober
{
	struct xrt_device *(*lelo_dallas_autoprobe)(struct xrt_prober *xdev);
	void (*destroy)(struct xrt_prober *xdev);
};

/*!
 * Call this function to create the prober. This function is setup in the the
 * very small target wrapper.c for each binary.
 *
 * @ingroup xrt_iface
 */
struct xrt_prober *
xrt_create_prober();


#ifdef __cplusplus
}
#endif
