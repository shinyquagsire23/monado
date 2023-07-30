// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR tracking source.
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_tracking
 */

#pragma once

#include "xrt/xrt_tracking.h"

#ifdef __cplusplus
extern "C" {
#endif

enum openvr_device
{
	T_OPENVR_DEVICE_UNKNOWN = 0,
	T_OPENVR_DEVICE_HMD,
	T_OPENVR_DEVICE_LEFT_CONTROLLER,
	T_OPENVR_DEVICE_RIGHT_CONTROLLER,
	T_OPENVR_DEVICE_TRACKER,
};

struct openvr_tracker;

/*!
 * Creates an OpenVR tracker.
 *
 * This creates an OpenVR instance in a separate
 * thread, and reports the tracking data of each device class `devs[i]` into the
 * pose sink `sinks[i]` at a rate of `sample_frequency`.
 *
 * @param sample_frequency_hz Sample frequency of the tracking data in hertz
 * @param devs Devices to report tracking data of
 * @param sinks Where to stream the tracking data of each device in `devs` to
 * @param sink_count Number of sinks/devices to track
 * @return struct openvr_tracker* if successfully created, null otherwise.
 */
struct openvr_tracker *
t_openvr_tracker_create(double sample_frequency_hz,
                        enum openvr_device *devs,
                        struct xrt_pose_sink **sinks,
                        int sink_count);

void
t_openvr_tracker_start(struct openvr_tracker *ovrt);

void
t_openvr_tracker_stop(struct openvr_tracker *ovrt);

void
t_openvr_tracker_destroy(struct openvr_tracker *ovrt);

#ifdef __cplusplus
}
#endif
