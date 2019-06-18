// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrapper around OS native hid functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 *
 * @ingroup aux_os
 */

#pragma once

#include "xrt/xrt_config.h"
#include "xrt/xrt_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Representing a single hid interface on a device.
 */
struct os_hid_device
{
	int (*read)(struct os_hid_device *hid_dev,
	            uint8_t *data,
	            size_t size,
	            int milliseconds);

	int (*write)(struct os_hid_device *hid_dev,
	             const uint8_t *data,
	             size_t size);

	void (*destroy)(struct os_hid_device *hid_dev);
};

/*!
 * Read from the given hid device, if milliseconds are negative blocks, 0 polls
 * and positive will block for that amount of seconds.
 */
XRT_MAYBE_UNUSED static inline int
os_hid_read(struct os_hid_device *hid_dev,
            uint8_t *data,
            size_t size,
            int milliseconds)
{
	return hid_dev->read(hid_dev, data, size, milliseconds);
}

/*!
 * Write to the given device.
 */
XRT_MAYBE_UNUSED static inline int
os_hid_write(struct os_hid_device *hid_dev, const uint8_t *data, size_t size)
{
	return hid_dev->write(hid_dev, data, size);
}

/*!
 * Close and free the given device.
 */
XRT_MAYBE_UNUSED static inline void
os_hid_destroy(struct os_hid_device *hid_dev)
{
	hid_dev->destroy(hid_dev);
}

#ifdef XRT_OS_LINUX
/*!
 * Open the given path as a hidraw device.
 */
int
os_hid_open_hidraw(const char *path, struct os_hid_device **out_hid);
#endif

#ifdef __cplusplus
} // extern "C"
#endif
