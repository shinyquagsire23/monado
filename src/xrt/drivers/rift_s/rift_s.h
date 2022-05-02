/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 * OpenHMD - Free and Open Source API and drivers for immersive technology.
 */

/*!
 * @file
 * @brief  Oculus Rift S Driver Internal Interface
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */

#pragma once

#include "os/os_threading.h"
#include "util/u_logging.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_frameserver.h"
#include "xrt/xrt_prober.h"
#include "xrt/xrt_tracking.h"

#include "rift_s_firmware.h"
#include "rift_s_protocol.h"
#include "rift_s_radio.h"
#include "rift_s_tracker.h"

#ifndef RIFT_S_H
#define RIFT_S_H

struct rift_s_hmd;
struct rift_s_controller;
struct rift_s_camera;

extern enum u_logging_level rift_s_log_level;

#define RIFT_S_TRACE(...) U_LOG_IFL_T(rift_s_log_level, __VA_ARGS__)
#define RIFT_S_DEBUG(...) U_LOG_IFL_D(rift_s_log_level, __VA_ARGS__)
#define RIFT_S_INFO(...) U_LOG_IFL_I(rift_s_log_level, __VA_ARGS__)
#define RIFT_S_WARN(...) U_LOG_IFL_W(rift_s_log_level, __VA_ARGS__)
#define RIFT_S_ERROR(...) U_LOG_IFL_E(rift_s_log_level, __VA_ARGS__)

#define MAX_TRACKED_DEVICES 2

#define HMD_HID 0
#define STATUS_HID 1
#define CONTROLLER_HID 2

/* All HMD Configuration / calibration info */
struct rift_s_hmd_config
{
	rift_s_panel_info_t panel_info;
	int proximity_threshold;

	/* Camera calibration block from firmware */
	struct rift_s_camera_calibration_block camera_calibration;

	struct rift_s_imu_config_info_t imu_config_info;
	struct rift_s_imu_calibration imu_calibration;
};

/* Structure to track online devices and type */
struct rift_s_tracked_device
{
	uint64_t device_id;
	rift_s_device_type device_type;
};

struct rift_s_system
{
	struct xrt_tracking_origin base;
	struct xrt_reference ref;

	/* Packet processing thread */
	struct os_thread_helper oth;
	struct os_hid_device *handles[3];
	uint64_t last_keep_alive;

	/* state tracking for tracked devices on our radio link */
	int num_active_tracked_devices;
	struct rift_s_tracked_device tracked_device[MAX_TRACKED_DEVICES];

	/* Radio comms manager */
	rift_s_radio_state radio_state;

	/* Device lock protects device access */
	struct os_mutex dev_mutex;

	/* All configuration data for the HMD, stored
	 * here for sharing to child objects */
	struct rift_s_hmd_config hmd_config;

	/* 3dof/SLAM tracker that provides HMD pose */
	struct rift_s_tracker *tracker;

	/* HMD device */
	struct rift_s_hmd *hmd;

	/* Controller devices */
	struct rift_s_controller *controllers[MAX_TRACKED_DEVICES];

	/* Video feed handling */
	struct xrt_frame_context xfctx;
	struct rift_s_camera *cam;
};

struct rift_s_system *
rift_s_system_create(struct xrt_prober *xp,
                     const unsigned char *hmd_serial_no,
                     struct os_hid_device *hid_hmd,
                     struct os_hid_device *hid_status,
                     struct os_hid_device *hid_controllers);

struct os_hid_device *
rift_s_system_hid_handle(struct rift_s_system *sys);
rift_s_radio_state *
rift_s_system_radio(struct rift_s_system *sys);

struct rift_s_tracker *
rift_s_system_get_tracker(struct rift_s_system *sys);

struct xrt_device *
rift_s_system_get_hmd(struct rift_s_system *sys);
void
rift_s_system_remove_hmd(struct rift_s_system *sys);

struct xrt_device *
rift_s_system_get_controller(struct rift_s_system *sys, int index);
void
rift_s_system_remove_controller(struct rift_s_system *sys, struct rift_s_controller *ctrl);

struct xrt_device *
rift_s_system_get_hand_tracking_device(struct rift_s_system *sys);

void
rift_s_system_reference(struct rift_s_system **dst, struct rift_s_system *src);

#endif
