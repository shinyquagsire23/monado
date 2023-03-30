// Copyright 2021 Jan Schmidt <jan@centricular.com>
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to read WMR cameras
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_wmr
 */

#pragma once

#include "util/u_debug.h"
#include "util/u_logging.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_frame.h"
#include "xrt/xrt_prober.h"

#include "wmr_config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wmr_camera;

struct wmr_camera_open_config
{
	struct xrt_prober_device *dev_holo;
	struct wmr_camera_config **tcam_confs; //!< Pointers to tracking cameras. Will be copied.
	struct xrt_frame_sink **tcam_sinks;    //!< Sinks for tracking cameras
	int tcam_count;                        //!< Tracking camera count
	int slam_cam_count;                    //!< Number of tracking cameras to use for SLAM
	enum u_logging_level log_level;
};

#ifdef XRT_HAVE_LIBUSB
struct wmr_camera *
wmr_camera_open(struct wmr_camera_open_config *config);
void
wmr_camera_free(struct wmr_camera *cam);

/*!
 * Starts the cameras.
 */
bool
wmr_camera_start(struct wmr_camera *cam);
bool
wmr_camera_stop(struct wmr_camera *cam);

/*!
 * Set manual exposure and gain values
 *
 * @param cam Camera container
 * @param camera_id Which camera to affect
 * @param exposure Time the shutter is open, observed values 60-6000.
 * @param gain Amplification of the analog signal, observed values: 16-255.
 */
int
wmr_camera_set_exposure_gain(struct wmr_camera *cam, uint8_t camera_id, uint16_t exposure, uint8_t gain);

#else

/* Stubs to disable camera functions without libusb */
#define wmr_camera_open(config) NULL
#define wmr_camera_free(cam)
#define wmr_camera_start(cam) false
#define wmr_camera_stop(cam) false
#define wmr_camera_set_exposure_gain(cam, camera_id, exposure, gain) -1

#endif

#ifdef __cplusplus
}
#endif
