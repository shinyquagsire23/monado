// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// Copyright 2021-2023, Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
//
/*!
 * @file
 * @brief Common implementation for WMR controllers, handling
 * shared behaviour such as communication, configuration reading,
 * IMU integration.
 * @author Jan Schmidt <jan@centricular.com>
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */
#pragma once

#include "os/os_threading.h"
#include "math/m_imu_3dof.h"
#include "util/u_logging.h"
#include "xrt/xrt_device.h"

#include "wmr_controller_protocol.h"
#include "wmr_config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wmr_controller_base;

/*!
 * A connection for communicating with the controller.
 * The mechanism is implementation specific, so there are
 * two variants for either communicating directly with a
 * controller via bluetooth, and another for talking
 * to a controller through a headset tunnelled mapping.
 *
 * The controller implementation doesn't need to care how
 * the communication is implemented.
 *
 * The HMD-tunnelled version of the connection is reference
 * counted and mutex protected, as both the controller and
 * the HMD need to hold a reference to it to clean up safely.
 * For bluetooth controllers, destruction of the controller
 * xrt_device calls disconnect and destroys the connection
 * object (and bluetooth listener) immediately.
 */
struct wmr_controller_connection
{
	//! The controller this connection is talking to.
	struct wmr_controller_base *wcb;

	bool (*send_bytes)(struct wmr_controller_connection *wcc, const uint8_t *buffer, uint32_t buf_size);
	void (*receive_bytes)(struct wmr_controller_connection *wcc,
	                      uint64_t time_ns,
	                      uint8_t *buffer,
	                      uint32_t buf_size);
	int (*read_sync)(struct wmr_controller_connection *wcc, uint8_t *buffer, uint32_t buf_size, int timeout_ms);

	void (*disconnect)(struct wmr_controller_connection *wcc);
};

static inline bool
wmr_controller_connection_send_bytes(struct wmr_controller_connection *wcc, const uint8_t *buffer, uint32_t buf_size)
{
	assert(wcc->send_bytes != NULL);
	return wcc->send_bytes(wcc, buffer, buf_size);
}

static inline int
wmr_controller_connection_read_sync(struct wmr_controller_connection *wcc,
                                    uint8_t *buffer,
                                    uint32_t buf_size,
                                    int timeout_ms)
{
	return wcc->read_sync(wcc, buffer, buf_size, timeout_ms);
}

static inline void
wmr_controller_connection_disconnect(struct wmr_controller_connection *wcc)
{
	wcc->disconnect(wcc);
}

/*!
 * Common base for all WMR controllers.
 *
 * @ingroup drv_wmr
 * @implements xrt_device
 */
struct wmr_controller_base
{
	//! Base struct.
	struct xrt_device base;

	//! Mutex protects the controller connection
	struct os_mutex conn_lock;

	//! The connection for this controller.
	struct wmr_controller_connection *wcc;

	//! Callback from the connection when a packet has been received.
	void (*receive_bytes)(struct wmr_controller_base *wcb, uint64_t time_ns, uint8_t *buffer, uint32_t buf_size);

	enum u_logging_level log_level;

	//! Mutex protects shared data used from OpenXR callbacks
	struct os_mutex data_lock;

	//! Callback to parse a controller update packet and update the input / imu info. Called with the
	//  data lock held.
	bool (*handle_input_packet)(struct wmr_controller_base *wcb,
	                            uint64_t time_ns,
	                            uint8_t *buffer,
	                            uint32_t buf_size);

	/* firmware configuration block */
	struct wmr_controller_config config;

	//! Time of last IMU sample, in CPU time.
	uint64_t last_imu_timestamp_ns;
	//! Main fusion calculator.
	struct m_imu_3dof fusion;
	//! The last angular velocity from the IMU, for prediction.
	struct xrt_vec3 last_angular_velocity;
};

bool
wmr_controller_base_init(struct wmr_controller_base *wcb,
                         struct wmr_controller_connection *conn,
                         enum xrt_device_type controller_type,
                         enum u_logging_level log_level);

void
wmr_controller_base_deinit(struct wmr_controller_base *wcb);

static inline void
wmr_controller_connection_receive_bytes(struct wmr_controller_connection *wcc,
                                        uint64_t time_ns,
                                        uint8_t *buffer,
                                        uint32_t buf_size)
{

	if (wcc->receive_bytes != NULL) {
		wcc->receive_bytes(wcc, time_ns, buffer, buf_size);
	} else {
		/* Default: deliver directly to the controller instance */
		struct wmr_controller_base *wcb = wcc->wcb;
		assert(wcb->receive_bytes != NULL);
		wcb->receive_bytes(wcb, time_ns, buffer, buf_size);
	}
}

#ifdef __cplusplus
}
#endif
