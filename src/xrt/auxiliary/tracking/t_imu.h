// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C interface to basic IMU fusion.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_tracking
 */

#include "math/m_api.h"

#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Opaque type for fusing IMU reports.
 */
struct imu_fusion;

/*!
 * Create a struct imu_fusion.
 *
 * @public @memberof imu_fusion
 * @ingroup aux_tracking
 */
struct imu_fusion *
imu_fusion_create();


/*!
 * Destroy a struct imu_fusion.
 *
 * Should not be called simultaneously with any other imu_fusion function.
 *
 * @param fusion The IMU Fusion object
 *
 * @public @memberof imu_fusion
 * @ingroup aux_tracking
 */
void
imu_fusion_destroy(struct imu_fusion *fusion);

/*!
 * Predict and correct fusion with a gyroscope reading.
 *
 * dt should not be zero: If you're receiving accel and gyro data at the same
 * time, call imu_fusion_incorporate_gyros_and_accelerometer() instead.
 *
 * Should not be called simultaneously with any other imu_fusion function.
 *
 * Non-zero return means error.
 *
 * @param fusion The IMU Fusion object
 * @param timestamp_ns The timestamp corresponding to the information being
 * processed with this call.
 * @param ang_vel Angular velocity vector from gyroscope: in radians per second.
 * @param ang_vel_variance The variance of the angular velocity measurements:
 * part of the characteristics of the IMU being used.
 *
 * @public @memberof imu_fusion
 * @ingroup aux_tracking
 */
int
imu_fusion_incorporate_gyros(struct imu_fusion *fusion,
                             uint64_t timestamp_ns,
                             struct xrt_vec3 const *ang_vel,
                             struct xrt_vec3 const *ang_vel_variance);

/*!
 * Predict and correct fusion with an accelerometer reading.
 *
 * If you're receiving accel and gyro data at the same time, call
 * imu_fusion_incorporate_gyros_and_accelerometer() instead.
 *
 * Should not be called simultaneously with any other imu_fusion function.
 *
 * Non-zero return means error.
 *
 * @param fusion The IMU Fusion object
 * @param timestamp_ns The timestamp corresponding to the information being
 * processed with this call.
 * @param accel Accelerometer data (in m/s/s) including the effect of gravity -
 * assumed to be +y when aligned with the world.
 * @param accel_variance The variance of the accelerometer measurements: part of
 * the characteristics of the IMU being used.
 * @param out_world_accel Optional output parameter: will contain the
 * non-gravity acceleration in the world frame.
 *
 * @public @memberof imu_fusion
 * @ingroup aux_tracking
 */
int
imu_fusion_incorporate_accelerometer(struct imu_fusion *fusion,
                                     uint64_t timestamp_ns,
                                     struct xrt_vec3 const *accel,
                                     struct xrt_vec3 const *accel_variance,
                                     struct xrt_vec3 *out_world_accel);

/*!
 * Predict and correct fusion with a simultaneous accelerometer and gyroscope
 * reading.
 *
 * Should not be called simultaneously with any other imu_fusion function.
 *
 * Non-zero return means error.
 *
 * @param fusion The IMU Fusion object
 * @param timestamp_ns The timestamp corresponding to the information being
 * processed with this call.
 * @param ang_vel Angular velocity vector from gyroscope: radians/s
 * @param ang_vel_variance The variance of the angular velocity measurements:
 * part of the characteristics of the IMU being used.
 * @param accel Accelerometer data (in m/s/s) including the effect of gravity -
 * assumed to be +y when aligned with the world.
 * @param accel_variance The variance of the accelerometer measurements: part of
 * the characteristics of the IMU being used.
 * @param out_world_accel Optional output parameter: will contain the
 * non-gravity acceleration in the world frame.
 *
 * @public @memberof imu_fusion
 * @ingroup aux_tracking
 */
int
imu_fusion_incorporate_gyros_and_accelerometer(struct imu_fusion *fusion,
                                               uint64_t timestamp_ns,
                                               struct xrt_vec3 const *ang_vel,
                                               struct xrt_vec3 const *ang_vel_variance,
                                               struct xrt_vec3 const *accel,
                                               struct xrt_vec3 const *accel_variance,
                                               struct xrt_vec3 *out_world_accel);

/*!
 * Get the predicted state. Does not advance the internal state clock.
 *
 * Non-zero return means error.
 *
 * @param fusion The IMU Fusion object
 * @param timestamp_ns The timestamp corresponding to the predicted state you
 * want.
 * @param out_quat The quaternion to populate with the predicted orientation.
 * @param out_ang_vel The vector to poluate with the predicted angular velocity.
 *
 * @public @memberof imu_fusion
 * @ingroup aux_tracking
 */
int
imu_fusion_get_prediction(struct imu_fusion const *fusion,
                          uint64_t timestamp_ns,
                          struct xrt_quat *out_quat,
                          struct xrt_vec3 *out_ang_vel);


/*!
 * Get the predicted state as a rotation vector. Does not advance the internal
 * state clock.
 *
 * This is mostly for debugging: a rotation vector can be easier to visualize or
 * understand intuitively.
 *
 * Non-zero return means error.
 *
 * @param fusion The IMU Fusion object
 * @param timestamp_ns The timestamp corresponding to the predicted state you
 * want.
 * @param out_rotation_vec The vector to poluate with the predicted orientation
 * rotation vector.
 *
 * @public @memberof imu_fusion
 * @ingroup aux_tracking
 */
int
imu_fusion_get_prediction_rotation_vec(struct imu_fusion const *fusion,
                                       uint64_t timestamp_ns,
                                       struct xrt_vec3 *out_rotation_vec);


#ifdef __cplusplus
}
#endif
