// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to remote driver.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_remote
 */

#pragma once

#include "xrt/xrt_defines.h"
#include "util/u_logging.h"


#ifdef __cplusplus
extern "C" {
#endif


struct xrt_system_devices;

/*!
 * @defgroup drv_remote Remote debugging driver
 * @ingroup drv
 *
 * @brief Driver for creating remote debugging devices.
 */

/*!
 * @dir drivers/remote
 *
 * @brief @ref drv_remote files.
 */

/*!
 * Header value to be set in the packet.
 *
 * @ingroup drv_remote
 */
#define R_HEADER_VALUE (*(uint64_t *)"mndrmt3\0")

/*!
 * Data per controller.
 */
struct r_remote_controller_data
{
	struct xrt_pose pose;
	struct xrt_vec3 linear_velocity;
	struct xrt_vec3 angular_velocity;

	float hand_curl[5];

	struct xrt_vec1 trigger_value;
	struct xrt_vec1 squeeze_value;
	struct xrt_vec1 squeeze_force;
	struct xrt_vec2 thumbstick;
	struct xrt_vec1 trackpad_force;
	struct xrt_vec2 trackpad;

	bool hand_tracking_active;
	bool active;

	bool system_click;
	bool system_touch;
	bool a_click;
	bool a_touch;
	bool b_click;
	bool b_touch;
	bool trigger_click;
	bool trigger_touch;
	bool thumbstick_click;
	bool thumbstick_touch;
	bool trackpad_touch;
	bool _pad0;
	bool _pad1;
	bool _pad2;
	// active(2) + bools(11) + pad(3) = 16
};

struct r_head_data
{
	struct
	{
		//! The field of view values of this view.
		struct xrt_fov fov;

		//! The pose of this view relative to @ref r_head_data::center.
		struct xrt_pose pose;

		//! Padded to fov(16) + pose(16 + 12) + 4 = 48
		uint32_t _pad;
	} views[2];

	//! The center of the head, in OpenXR terms the view space.
	struct xrt_pose center;

	//! Is the per view data valid and should be used?
	bool per_view_data_valid;

	//! pose(16 + 12) bool(1) + pad(3) = 32.
	bool _pad0, _pad1, _pad2;
};

/*!
 * Remote data sent from the debugger to the hub.
 *
 * @ingroup drv_remote
 */
struct r_remote_data
{
	uint64_t header;

	struct r_head_data head;

	struct r_remote_controller_data left, right;
};

/*!
 * Shared connection.
 *
 * @ingroup drv_remote
 */
struct r_remote_connection
{
	//! Logging level to be used.
	enum u_logging_level log_level;

	//! Socket.
	int fd;
};

/*!
 * Creates the remote system devices.
 *
 * @ingroup drv_remote
 */
xrt_result_t
r_create_devices(uint16_t port, struct xrt_system_devices **out_xsysd);

/*!
 * Initializes and connects the connection.
 *
 * @ingroup drv_remote
 */
int
r_remote_connection_init(struct r_remote_connection *rc, const char *addr, uint16_t port);

int
r_remote_connection_read_one(struct r_remote_connection *rc, struct r_remote_data *data);

int
r_remote_connection_write_one(struct r_remote_connection *rc, const struct r_remote_data *data);


#ifdef __cplusplus
}
#endif
