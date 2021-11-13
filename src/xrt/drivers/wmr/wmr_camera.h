// Copyright 2021 Jan Schmidt <jan@centricular.com>
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to read WMR cameras
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_wmr
 */

#pragma once

#include "xrt/xrt_config_have.h"
#include "util/u_logging.h"
#include "xrt/xrt_prober.h"

#include "wmr_config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wmr_camera;

#ifdef XRT_HAVE_LIBUSB
struct wmr_camera *
wmr_camera_open(struct xrt_prober_device *dev_holo, enum u_logging_level ll);
void
wmr_camera_free(struct wmr_camera *cam);

bool
wmr_camera_start(struct wmr_camera *cam, struct wmr_camera_config *cam_configs, int n_configs);
bool
wmr_camera_stop(struct wmr_camera *cam);
#else

/* Stubs to disable camera functions without libusb */
#define wmr_camera_open(dev_holo, ll) NULL
#define wmr_camera_free(cam)
#define wmr_camera_start(cam, cam_configs, n_configs) false
#define wmr_camera_stop(cam) false

#endif

#ifdef __cplusplus
}
#endif
