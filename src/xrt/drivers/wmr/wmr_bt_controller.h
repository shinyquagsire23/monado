// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// Copyright 2020-2023, Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Driver interface for Bluetooth based WMR motion controllers.
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */
#pragma once

#include "os/os_threading.h"
#include "util/u_logging.h"

#include "wmr_controller_base.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * A connection to a Bluetooth connected WMR Controller device
 *
 * @ingroup drv_wmr
 * @implements wmr_controller_connection
 */
struct wmr_bt_connection
{
	struct wmr_controller_connection base;

	enum u_logging_level log_level;

	struct os_hid_device *controller_hid;
	struct os_thread_helper controller_thread;

	struct os_mutex hid_lock;
};

struct xrt_device *
wmr_bt_controller_create(struct os_hid_device *controller_hid,
                         enum xrt_device_type controller_type,
                         uint16_t vid,
                         uint16_t pid,
                         enum u_logging_level log_level);

#ifdef __cplusplus
}
#endif
