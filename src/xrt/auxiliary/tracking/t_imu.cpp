// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  IMU fusion implementation - for inclusion into the single
 * kalman-incuding translation unit.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_tracking
 */

#include "tracking/t_imu.h"
#include "tracking/t_imu_fusion.hpp"

#include "math/m_eigen_interop.hpp"
#include "util/u_misc.h"

#include <memory>


struct imu_fusion
{
public:
	uint64_t time_ns{0};

	xrt_fusion::SimpleIMUFusion simple_fusion;


public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

/*
 * API functions
 */
struct imu_fusion *
imu_fusion_create()
{
	try {
		auto fusion = std::make_unique<imu_fusion>();
		return fusion.release();
	} catch (...) {
		return NULL;
	}
}

void
imu_fusion_destroy(struct imu_fusion *fusion)
{
	try {
		delete fusion;
	} catch (...) {
		assert(false && "Caught exception on destroy");
	}
}
int
imu_fusion_incorporate_gyros(struct imu_fusion *fusion,
                             uint64_t timestamp_ns,
                             struct xrt_vec3 const *ang_vel,
                             struct xrt_vec3 const *ang_vel_variance)
{
	try {
		assert(fusion);
		assert(ang_vel);
		assert(ang_vel_variance);
		assert(timestamp_ns != 0);

		fusion->simple_fusion.handleGyro(map_vec3(*ang_vel).cast<double>(), timestamp_ns);
		return 0;
	} catch (...) {
		assert(false && "Caught exception on incorporate gyros");
		return -1;
	}
}

int
imu_fusion_incorporate_accelerometer(struct imu_fusion *fusion,
                                     uint64_t timestamp_ns,
                                     struct xrt_vec3 const *accel,
                                     struct xrt_vec3 const *accel_variance,
                                     struct xrt_vec3 *out_world_accel)
{
	try {
		assert(fusion);
		assert(accel);
		assert(accel_variance);
		assert(timestamp_ns != 0);
		Eigen::Vector3d accelVec = map_vec3(*accel).cast<double>();
		fusion->simple_fusion.handleAccel(accelVec, timestamp_ns);
		if (out_world_accel != NULL) {
			Eigen::Vector3d worldAccel = fusion->simple_fusion.getCorrectedWorldAccel(accelVec);
			map_vec3(*out_world_accel) = worldAccel.cast<float>();
		}
		return 0;
	} catch (...) {
		assert(false && "Caught exception on incorporate accelerometer");
		return -1;
	}
}

int
imu_fusion_get_prediction(struct imu_fusion const *fusion,
                          uint64_t timestamp_ns,
                          struct xrt_quat *out_quat,
                          struct xrt_vec3 *out_ang_vel)
{
	try {
		assert(fusion);
		assert(out_quat);
		assert(out_ang_vel);
		assert(timestamp_ns != 0);
		if (!fusion->simple_fusion.valid()) {
			return -2;
		}

		map_vec3(*out_ang_vel) = fusion->simple_fusion.getAngVel().cast<float>();

		if (timestamp_ns == fusion->time_ns) {
			// No need to predict here.
			map_quat(*out_quat) = fusion->simple_fusion.getQuat().cast<float>();
			return 0;
		}
		Eigen::Quaterniond predicted_quat = fusion->simple_fusion.getPredictedQuat(timestamp_ns);
		map_quat(*out_quat) = predicted_quat.cast<float>();
		return 0;

	} catch (...) {
		assert(false && "Caught exception on getting prediction");
		return -1;
	}
}

int
imu_fusion_get_prediction_rotation_vec(struct imu_fusion const *fusion,
                                       uint64_t timestamp_ns,
                                       struct xrt_vec3 *out_rotation_vec)
{
	try {
		assert(fusion);
		assert(out_rotation_vec);
		assert(timestamp_ns != 0);

		if (!fusion->simple_fusion.valid()) {
			return -2;
		}
		if (timestamp_ns == fusion->time_ns) {
			// No need to predict here.
			map_vec3(*out_rotation_vec) = fusion->simple_fusion.getRotationVec().cast<float>();
		} else {
			Eigen::Quaterniond predicted_quat = fusion->simple_fusion.getPredictedQuat(timestamp_ns);
			map_vec3(*out_rotation_vec) = flexkalman::util::quat_ln(predicted_quat).cast<float>();
		}
		return 0;
	} catch (...) {
		assert(false && "Caught exception on getting prediction");
		return -1;
	}
}

int
imu_fusion_incorporate_gyros_and_accelerometer(struct imu_fusion *fusion,
                                               uint64_t timestamp_ns,
                                               struct xrt_vec3 const *ang_vel,
                                               struct xrt_vec3 const *ang_vel_variance,
                                               struct xrt_vec3 const *accel,
                                               struct xrt_vec3 const *accel_variance,
                                               struct xrt_vec3 *out_world_accel)
{
	try {
		assert(fusion);
		assert(ang_vel);
		assert(ang_vel_variance);
		assert(accel);
		assert(accel_variance);
		assert(timestamp_ns != 0);

		Eigen::Vector3d accelVec = map_vec3(*accel).cast<double>();
		Eigen::Vector3d angVelVec = map_vec3(*ang_vel).cast<double>();
		fusion->simple_fusion.handleAccel(accelVec, timestamp_ns);
		fusion->simple_fusion.handleGyro(angVelVec, timestamp_ns);
		fusion->simple_fusion.postCorrect();
		if (out_world_accel != NULL) {
			Eigen::Vector3d worldAccel = fusion->simple_fusion.getCorrectedWorldAccel(accelVec);
			map_vec3(*out_world_accel) = worldAccel.cast<float>();
		}
		return 0;
	} catch (...) {
		assert(false && "Caught exception on incorporate gyros and accelerometer");
		return -1;
	}
}
