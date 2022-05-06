// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header for system objects.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_defines.h"


#define XRT_SYSTEM_MAX_DEVICES (32)

/*!
 * A collection of @ref xrt_device, and the roles they have been assigned.
 *
 * @see xrt_device, xrt_instance.
 */
struct xrt_system_devices
{
	struct xrt_device *xdevs[XRT_SYSTEM_MAX_DEVICES];
	size_t xdev_count;

	struct
	{
		struct xrt_device *head;
		struct xrt_device *left;
		struct xrt_device *right;
		struct xrt_device *gamepad;

		struct
		{
			struct xrt_device *left;
			struct xrt_device *right;
		} hand_tracking;
	} roles;


	/*!
	 * Destroy all the devices that are owned by this system devices.
	 *
	 * Code consuming this interface should use xrt_system_devices_destroy.
	 */
	void (*destroy)(struct xrt_system_devices *xsysd);
};


/*!
 * Destroy an xrt_system_devices and owned devices - helper function.
 *
 * @param[in,out] xsysd_ptr A pointer to the xrt_system_devices struct pointer.
 *
 * Will destroy the system devices if *xsysd_ptr is not NULL. Will then set
 * *xsysd_ptr to NULL.
 *
 * @public @memberof xrt_system_devices
 */
static inline void
xrt_system_devices_destroy(struct xrt_system_devices **xsysd_ptr)
{
	struct xrt_system_devices *xsysd = *xsysd_ptr;
	if (xsysd == NULL) {
		return;
	}

	*xsysd_ptr = NULL;
	xsysd->destroy(xsysd);
}
