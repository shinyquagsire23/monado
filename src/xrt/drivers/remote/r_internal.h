// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal stuff in remote driver.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_remote
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_tracking.h"

#include "os/os_threading.h"

#include "util/u_hand_tracking.h"

#include "r_interface.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Central object remote object.
 *
 * @ingroup drv_remote
 */
struct r_hub
{
	struct xrt_tracking_origin base;

	//! Connection to the controller.
	struct r_remote_connection rc;

	//! The data that the is the reset position.
	struct r_remote_data reset;

	//! The latest data received.
	struct r_remote_data latest;


	int accept_fd;

	uint16_t port;

	struct os_thread_helper oth;

	struct
	{
		bool hmd, left, right;
	} gui;
};

/*!
 * HMD
 *
 * @ingroup drv_remote
 */
struct r_hmd
{
	struct xrt_device base;

	struct r_hub *r;
};

/*!
 * Device
 *
 * @ingroup drv_remote
 */
struct r_device
{
	struct xrt_device base;

	struct r_hub *r;

	struct u_hand_tracking hand_tracking;

	bool is_left;
};


struct xrt_device *
r_hmd_create(struct r_hub *r);

struct xrt_device *
r_device_create(struct r_hub *r, bool is_left);


#ifdef __cplusplus
}
#endif
