/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 */

/*!
 * @file
 * @brief  Oculus Rift S camera handling
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_rift_s
 */

#pragma once

#include "os/os_hid.h"
#include "xrt/xrt_prober.h"

#include "rift_s_firmware.h"
#include "rift_s_tracker.h"

struct rift_s_camera;

struct rift_s_camera *
rift_s_camera_create(struct xrt_prober *xp,
                     struct xrt_frame_context *xfctx,
                     const char *hmd_serial_no,
                     struct os_hid_device *hid,
                     struct rift_s_tracker *tracker,
                     struct rift_s_camera_calibration_block *camera_calibration);

void
rift_s_camera_destroy(struct rift_s_camera *cam);

void
rift_s_camera_update(struct rift_s_camera *cam, struct os_hid_device *hid);
