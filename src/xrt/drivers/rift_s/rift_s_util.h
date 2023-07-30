/*
 * Copyright 2022 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 */

/*!
 * @file
 * @brief  Oculus Rift S utility functions
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */

#pragma once

#include "tracking/t_tracking.h"

#include "rift_s_firmware.h"

#ifdef __cplusplus
extern "C" {
#endif

struct t_camera_calibration
rift_s_get_cam_calib(struct rift_s_camera_calibration_block *camera_calibration, enum rift_s_camera_id cam_id);

struct t_stereo_camera_calibration *
rift_s_create_stereo_camera_calib_rotated(struct rift_s_camera_calibration_block *camera_calibration);

#ifdef __cplusplus
}
#endif
