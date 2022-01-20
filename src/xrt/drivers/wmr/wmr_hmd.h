// Copyright 2018, Philipp Zabel.
// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to the WMR HMD driver code.
 * @author Philipp Zabel <philipp.zabel@gmail.com>
 * @author nima01 <nima_zero_one@protonmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_wmr
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_prober.h"
#include "os/os_threading.h"
#include "math/m_imu_3dof.h"
#include "util/u_logging.h"
#include "util/u_distortion_mesh.h"
#include "util/u_var.h"

#include "wmr_protocol.h"
#include "wmr_config.h"
#include "wmr_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

enum wmr_headset_type
{
	WMR_HEADSET_GENERIC,
	WMR_HEADSET_REVERB_G1,
	WMR_HEADSET_REVERB_G2,
	WMR_HEADSET_SAMSUNG_800ZAA,
	WMR_HEADSET_LENOVO_EXPLORER,
	WMR_HEADSET_MEDION_ERAZER_X1000,
};

struct wmr_hmd;

struct wmr_headset_descriptor
{
	enum wmr_headset_type hmd_type;

	//! String by which we recognise the device
	const char *dev_id_str;
	//! Friendly ID string for debug
	const char *debug_name;

	int (*init_func)(struct wmr_hmd *wh);
	void (*deinit_func)(struct wmr_hmd *wh);
};

struct wmr_hmd_distortion_params
{
	//! Inverse affine transform to move from (undistorted) pixels
	//! to image plane / normalised image coordinates
	struct xrt_matrix_3x3 inv_affine_xform;

	//! tan(angle) FoV min/max for X and Y in the input texture
	struct xrt_vec2 tex_x_range;
	struct xrt_vec2 tex_y_range;
};

/*!
 * @implements xrt_device
 */
struct wmr_hmd
{
	struct xrt_device base;

	const struct wmr_headset_descriptor *hmd_desc;

	//! firmware configuration block, with device names etc
	struct wmr_config_header config_hdr;

	//! Config data parsed from the firmware JSON
	struct wmr_hmd_config config;

	//! Packet reading thread.
	struct os_thread_helper oth;

	enum u_logging_level log_level;

	/*!
	 * This is the hololens sensor device, this is were we get all of the
	 * IMU data and read the config from.
	 *
	 * During start it is owned by the thread creating the device, after
	 * init it is owned by the reading thread, there is no mutex protecting
	 * this field as it's only used by the reading thread in @p oth.
	 */
	struct os_hid_device *hid_hololens_sensors_dev;
	struct os_hid_device *hid_control_dev;

	//! Latest raw IPD value from the device.
	uint16_t raw_ipd;
	//! Latest proximity sensor value from the device.
	uint8_t proximity_sensor;

	//! Distortion related parameters
	struct wmr_hmd_distortion_params distortion_params[2];

	// Config-derived poses
	struct xrt_pose centerline;
	struct xrt_pose display_to_centerline[2];
	struct xrt_pose accel_to_centerline;
	struct xrt_pose gyro_to_centerline;
	struct xrt_pose mag_to_centerline;

	struct hololens_sensors_packet packet;

	struct
	{
		//! Protects all members of the `fusion` substruct.
		struct os_mutex mutex;

		//! Main fusion calculator.
		struct m_imu_3dof i3dof;

		//! The last angular velocity from the IMU, for prediction.
		struct xrt_vec3 last_angular_velocity;

		//! When did we get the last IMU sample, in CPU time.
		uint64_t last_imu_timestamp_ns;
	} fusion;

	struct
	{
		//! Source of video/IMU data for SLAM
		struct xrt_fs *source;

		//! Context for @ref source
		struct xrt_frame_context xfctx;

		//! SLAM tracker
		struct xrt_tracked_slam *tracker;

		//! Set at start. Whether the tracker was initialized.
		bool enabled;
	} slam;

	//! Whether to use the @ref slam tracker or fallback to the @ref fusion 3dof tracker
	bool use_slam_tracker;

	//! Last tracked pose
	struct xrt_pose pose;

	//! Additional offset to apply to `pose`
	struct xrt_pose offset;

	struct
	{
		struct u_var_button switch_tracker_btn;
	} gui;
};

static inline struct wmr_hmd *
wmr_hmd(struct xrt_device *p)
{
	return (struct wmr_hmd *)p;
}

struct xrt_device *
wmr_hmd_create(enum wmr_headset_type hmd_type,
               struct os_hid_device *hid_holo,
               struct os_hid_device *hid_ctrl,
               struct xrt_prober_device *dev_holo,
               enum u_logging_level log_level);

#define WMR_TRACE(d, ...) U_LOG_XDEV_IFL_T(&d->base, d->log_level, __VA_ARGS__)
#define WMR_DEBUG(d, ...) U_LOG_XDEV_IFL_D(&d->base, d->log_level, __VA_ARGS__)
#define WMR_INFO(d, ...) U_LOG_XDEV_IFL_I(&d->base, d->log_level, __VA_ARGS__)
#define WMR_WARN(d, ...) U_LOG_XDEV_IFL_W(&d->base, d->log_level, __VA_ARGS__)
#define WMR_ERROR(d, ...) U_LOG_XDEV_IFL_E(&d->base, d->log_level, __VA_ARGS__)


#ifdef __cplusplus
}
#endif
