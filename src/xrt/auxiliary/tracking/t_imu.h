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
 * @public @memberof imu_fusion
 * @ingroup aux_tracking
 */
int
imu_fusion_incorporate_gyros(struct imu_fusion *fusion,
                             uint64_t timestamp_ns,
                             struct xrt_vec3 const *ang_vel,
                             struct xrt_vec3 const *variance);

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
 * @public @memberof imu_fusion
 * @ingroup aux_tracking
 */
int
imu_fusion_incorporate_accelerometer(struct imu_fusion *fusion,
                                     uint64_t timestamp_ns,
                                     struct xrt_vec3 const *accel,
                                     struct xrt_vec3 const *variance);

/*!
 * Predict and correct fusion with a simultaneous accelerometer and gyroscope
 * reading.
 *
 * Should not be called simultaneously with any other imu_fusion function.
 *
 * Non-zero return means error.
 *
 * @public @memberof imu_fusion
 * @ingroup aux_tracking
 */
int
imu_fusion_incorporate_gyros_and_accelerometer(
    struct imu_fusion *fusion,
    uint64_t timestamp_ns,
    struct xrt_vec3 const *ang_vel,
    struct xrt_vec3 const *ang_vel_variance,
    struct xrt_vec3 const *accel,
    struct xrt_vec3 const *accel_variance);

/*!
 * Get the predicted state. Does not advance the internal state clock.
 *
 * Non-zero return means error.
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
 * Non-zero return means error.
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
