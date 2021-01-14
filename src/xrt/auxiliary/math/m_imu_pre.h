// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  IMU pre filter struct.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#include "xrt/xrt_defines.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * This is a common IMU pre-filter which takes raw 'ticks' from a 3 axis IMU
 * measurement and converts it into degrees per secs and meters per floats.
 *
 * One of these is used per gyro, accelerometer and magnometer.
 *
 * The formula used is: `v = ((V * ticks_to_float) - bias) * gain`. For
 * @ref ticks_to_float the same value used for all channels, where as for
 * @ref gain and @ref bias the value is per channel.
 */
struct m_imu_pre_filter_part
{
	//! Bias for the imu part.
	struct xrt_vec3 bias;
	float _pad;
	//! Gain for the imu part.
	struct xrt_vec3 gain;
	//! Going from IMU 'ticks' to a floating value.
	float ticks_to_float;
};

/*!
 * This is a common IMU pre-filter which takes raw 'ticks' from an IMU
 * measurement and converts it into floats representing radians per second and
 * meters per second^2 floats.
 */
struct m_imu_pre_filter
{
	struct m_imu_pre_filter_part accel;
	struct m_imu_pre_filter_part gyro;

	/*!
	 * A transform on how to flip axis and rotate the IMU values into device
	 * space.
	 */
	struct xrt_matrix_3x3 transform;
};

/*!
 * A simple init function that just takes the two ticks_to_float values, all
 * other values are set to identity.
 */
void
m_imu_pre_filter_init(struct m_imu_pre_filter *imu, float ticks_to_float_accel, float ticks_to_float_gyro);

/*!
 * Sets the transformation to flip X and Y axis. This changes the handedness
 * of the coordinates.
 */
void
m_imu_pre_filter_set_switch_x_and_y(struct m_imu_pre_filter *imu);

/*!
 * Pre-filters the values, taking them from ticks into float values.
 *
 * See description of @ref m_imu_pre_filter_part for formula used. Also rotates
 * values with the imu_to_head pose.
 */
void
m_imu_pre_filter_data(struct m_imu_pre_filter *imu,
                      const struct xrt_vec3_i32 *accel,
                      const struct xrt_vec3_i32 *gyro,
                      struct xrt_vec3 *out_accel,
                      struct xrt_vec3 *out_gyro);


#ifdef __cplusplus
}
#endif
