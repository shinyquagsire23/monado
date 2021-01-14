// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrapper around OS native BLE functions.
 * @author Pete Black <pete.black@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_os
 */

#pragma once

#include "xrt/xrt_config_os.h"
#include "xrt/xrt_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * @interface os_ble_device
 * Representing a single ble notify attribute on a device.
 *
 * @ingroup aux_os
 */
struct os_ble_device
{
	int (*read)(struct os_ble_device *ble_dev, uint8_t *data, size_t size, int milliseconds);

	void (*destroy)(struct os_ble_device *ble_dev);
};

/*!
 * Read data from the ble file descriptor, if any, from the given bledevice.
 *
 * If milliseconds are negative, this call blocks indefinitely, 0 polls,
 * and positive will block for that amount of milliseconds.
 *
 * @ingroup aux_os
 */
XRT_MAYBE_UNUSED static inline int
os_ble_read(struct os_ble_device *ble_dev, uint8_t *data, size_t size, int milliseconds)
{
	return ble_dev->read(ble_dev, data, size, milliseconds);
}

/*!
 * Close and free the given device, does null checking and zeroing.
 *
 * @ingroup aux_os
 */
XRT_MAYBE_UNUSED static inline void
os_ble_destroy(struct os_ble_device **ble_dev_ptr)
{
	struct os_ble_device *ble_dev = *ble_dev_ptr;
	if (ble_dev == NULL) {
		return;
	}

	ble_dev->destroy(ble_dev);
	*ble_dev_ptr = NULL;
}

#ifdef XRT_OS_LINUX
/*!
 * Returns a notification endpoint from the given device uuid and char uuid.
 *
 * @returns Negative on failure, zero on no device found and positive if a
 *          device has been found.
 *
 * @ingroup aux_os
 */
int
os_ble_notify_open(const char *dev_uuid, const char *char_uuid, struct os_ble_device **out_ble);

/*!
 * Returns write startpoints from the given device uuid and char uuid.
 *
 * @returns Negative on failure, zero on no device found and positive if a
 *          device has been found.
 *
 * @ingroup aux_os
 */
int
os_ble_broadcast_write_value(const char *dev_uuid, const char *char_uuid, uint8_t value);
#endif


#ifdef __cplusplus
}
#endif
