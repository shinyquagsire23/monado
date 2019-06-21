// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrapper around OS native hid functions.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
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

	int (*get_feature)(struct os_hid_device *hid_dev,
	                   uint8_t report_num,
	                   uint8_t *data,
	                   size_t size);

	int (*set_feature)(struct os_hid_device *hid_dev,
	                   const uint8_t *data,
	                   size_t size);

	void (*destroy)(struct os_hid_device *hid_dev);
};

/*!
 * Read the next input report, if any, from the given hid device.
 *
 * If milliseconds are negative, this call blocks indefinitely, 0 polls,
 * and positive will block for that amount of milliseconds.
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
 * Write an output report to the given device.
 */
XRT_MAYBE_UNUSED static inline int
os_hid_write(struct os_hid_device *hid_dev, const uint8_t *data, size_t size)
{
	return hid_dev->write(hid_dev, data, size);
}

/*!
 * Get a numbered feature report.
 *
 * If the device doesn't have more than one feature report, just request
 * report 0.
 */
XRT_MAYBE_UNUSED static inline int
os_hid_get_feature(struct os_hid_device *hid_dev,
                   uint8_t report_num,
                   uint8_t *data,
                   size_t size)
{
	return hid_dev->get_feature(hid_dev, report_num, data, size);
}

/*!
 * Set a feature report.
 *
 * The first byte of the buffer is the report number, to be followed by
 * the data of the report.
 */
XRT_MAYBE_UNUSED static inline int
os_hid_set_feature(struct os_hid_device *hid_dev,
                   const uint8_t *data,
                   size_t size)
{
	return hid_dev->set_feature(hid_dev, data, size);
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
