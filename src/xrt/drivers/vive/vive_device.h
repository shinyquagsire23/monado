// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  vive device header
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @ingroup drv_vive
 */

#pragma once

#include "xrt/xrt_device.h"
#include "os/os_threading.h"
#include "util/u_logging.h"
#include "util/u_distortion_mesh.h"
#include "math/m_imu_3dof.h"
#include "math/m_relation_history.h"

#include "vive/vive_config.h"

#include "vive_lighthouse.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @implements xrt_device
 */
struct vive_device
{
	struct xrt_device base;
	struct os_hid_device *mainboard_dev;
	struct os_hid_device *sensors_dev;
	struct os_hid_device *watchman_dev;

	struct lighthouse_watchman watchman;

	struct os_thread_helper sensors_thread;
	struct os_thread_helper watchman_thread;
	struct os_thread_helper mainboard_thread;

	struct
	{
		timepoint_ns last_sample_ts_ns;
		uint32_t last_sample_ticks;
		uint8_t sequence;
	} imu;

	struct
	{
		uint16_t ipd;
		uint16_t lens_separation;
		uint16_t proximity;
		uint8_t button;
	} board;

	enum u_logging_level log_level;
	bool disconnect_notified;

	struct
	{
		bool calibration;
		bool fusion;
	} gui;

	struct vive_config config;

	struct
	{
		//! Protects all members of the `fusion` substruct.
		struct os_mutex mutex;

		//! Main fusion calculator.
		struct m_imu_3dof i3dof;

		//! Prediction
		struct m_relation_history *relation_hist;
	} fusion;
};

struct vive_device *
vive_device_create(struct os_hid_device *mainboard_dev,
                   struct os_hid_device *sensors_dev,
                   struct os_hid_device *watchman_dev,
                   enum VIVE_VARIANT variant);

#ifdef __cplusplus
}
#endif
