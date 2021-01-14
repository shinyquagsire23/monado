// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  C++ sensor fusion/filtering code that uses flexkalman
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_tracking
 */

#pragma once

#ifndef __cplusplus
#error "This header is C++-only."
#endif

#include "tracking/t_lowpass.hpp"
#include "tracking/t_lowpass_vector.hpp"
#include "math/m_api.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_logging.h"

#include <Eigen/Core>
#include <Eigen/Geometry>

#include "flexkalman/EigenQuatExponentialMap.h"

DEBUG_GET_ONCE_LOG_OPTION(simple_imu_log, "SIMPLE_IMU_LOG", U_LOGGING_WARN)

#define SIMPLE_IMU_TRACE(...) U_LOG_IFL_T(ll, __VA_ARGS__)
#define SIMPLE_IMU_DEBUG(...) U_LOG_IFL_D(ll, __VA_ARGS__)
#define SIMPLE_IMU_INFO(...) U_LOG_IFL_I(ll, __VA_ARGS__)
#define SIMPLE_IMU_WARN(...) U_LOG_IFL_W(ll, __VA_ARGS__)
#define SIMPLE_IMU_ERROR(...) U_LOG_IFL_E(ll, __VA_ARGS__)

namespace xrt_fusion {
class SimpleIMUFusion
{
public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	/*!
	 * @param gravity_rate Value in [0, 1] indicating how much the
	 * accelerometer should affect the orientation each second.
	 */
	explicit SimpleIMUFusion(double gravity_rate = 0.9)
	    : gravity_scale_(gravity_rate), ll(debug_get_log_option_simple_imu_log())
	{
		SIMPLE_IMU_DEBUG("Creating instance");
	}

	/*!
	 * @return true if the filter has started up
	 */
	bool
	valid() const noexcept
	{
		return started_;
	}

	/*!
	 * Get the current state orientation.
	 */
	Eigen::Quaterniond
	getQuat() const
	{
		return quat_;
	}

	/*!
	 * Get the current state orientation as a rotation vector.
	 */
	Eigen::Vector3d
	getRotationVec() const
	{
		return flexkalman::util::quat_ln(quat_);
	}

	/*!
	 * Get the current state orientation as a rotation matrix.
	 */
	Eigen::Matrix3d
	getRotationMatrix() const
	{
		return quat_.toRotationMatrix();
	}

	/*!
	 * @brief Get the predicted orientation at some point in the future.
	 *
	 * Here, we do **not** clamp the delta-t, so only ask for reasonable
	 * points in the future. (The gyro handler math does clamp delta-t for
	 * the purposes of integration in case of long times without signals,
	 * etc, which is OK since the accelerometer serves as a correction.)
	 */
	Eigen::Quaterniond
	getPredictedQuat(timepoint_ns timestamp) const;

	/*!
	 * Get the angular velocity in body space.
	 */
	Eigen::Vector3d const &
	getAngVel() const
	{
		return angVel_;
	}

	/*!
	 * Process a gyroscope report.
	 *
	 * @note At startup, no gyro reports will be considered until at least
	 * one accelerometer report has been processed, to provide us with an
	 * initial estimate of the direction of gravity.
	 *
	 * @param gyro Angular rate in radians per second, in body space.
	 * @param timestamp Nanosecond timestamp of this measurement.
	 *
	 * @return true if the report was used to update the state.
	 */
	bool
	handleGyro(Eigen::Vector3d const &gyro, timepoint_ns timestamp);


	/*!
	 * Process an accelerometer report.
	 *
	 * @param accel Body-relative acceleration measurement in m/s/s.
	 * @param timestamp Nanosecond timestamp of this measurement.
	 *
	 * @return true if the report was used to update the state.
	 */
	bool
	handleAccel(Eigen::Vector3d const &accel, timepoint_ns timestamp);

	/*!
	 * Use this to obtain the residual, world-space acceleration in m/s/s
	 * **not** associated with gravity, after incorporating a measurement.
	 *
	 * @param accel Body-relative acceleration measurement in m/s/s.
	 */
	Eigen::Vector3d
	getCorrectedWorldAccel(Eigen::Vector3d const &accel) const
	{
		Eigen::Vector3d adjusted_accel = accel * getAccelScaleFactor_();
		return (quat_ * adjusted_accel) - (Eigen::Vector3d::UnitY() * MATH_GRAVITY_M_S2);
	}

	/*!
	 * @brief Normalize internal state.
	 *
	 * Call periodically, like after you finish handling both the accel and
	 * gyro from one packet.
	 */
	void
	postCorrect()
	{
		quat_.normalize();
	}

private:
	/*!
	 * Returns a coefficient to correct the scale of the accelerometer
	 * reading.
	 */
	double
	getAccelScaleFactor_() const
	{
		// For a "perfect" accelerometer, gravity_filter_.getState()
		// should return MATH_GRAVITY_M_S2, making this method return 1.
		return MATH_GRAVITY_M_S2 / gravity_filter_.getState();
	}

	//! Body-space angular velocity in radian/s
	Eigen::Vector3d angVel_{Eigen::Vector3d::Zero()};
	//! Current orientation
	Eigen::Quaterniond quat_{Eigen::Quaterniond::Identity()};
	double gravity_scale_;

	/*!
	 * @brief Low-pass filter for extracting the gravity direction from the
	 * full accel signal.
	 *
	 * High-frequency components of the accel are either noise or
	 * user-caused acceleration, and do not reflect the direction of
	 * gravity.
	 */
	LowPassIIRVectorFilter<3, double> accel_filter_{200 /* hz cutoff frequency */};

	/*!
	 * @brief Even-lower low pass filter on the length of the acceleration
	 * vector, used to estimate a corrective scale for the accelerometer
	 * data.
	 *
	 * Over time, the length of the accelerometer data will average out to
	 * be the acceleration due to gravity.
	 */
	LowPassIIRFilter<double> gravity_filter_{1 /* hz cutoff frequency */};
	uint64_t last_accel_timestamp_{0};
	uint64_t last_gyro_timestamp_{0};
	double gyro_min_squared_length_{1.e-8};
	bool started_{false};
	enum u_logging_level ll;
};

inline Eigen::Quaterniond
SimpleIMUFusion::getPredictedQuat(timepoint_ns timestamp) const
{
	timepoint_ns state_time = std::max(last_accel_timestamp_, last_gyro_timestamp_);
	if (state_time == 0) {
		// no data yet.
		return Eigen::Quaterniond::Identity();
	}
	time_duration_ns delta_ns = timestamp - state_time;
	float dt = time_ns_to_s(delta_ns);
	return quat_ * flexkalman::util::quat_exp(angVel_ * dt * 0.5);
}
inline bool
SimpleIMUFusion::handleGyro(Eigen::Vector3d const &gyro, timepoint_ns timestamp)
{
	if (!started_) {

		SIMPLE_IMU_DEBUG(
		    "Discarding gyro report before first usable accel "
		    "report");
		return false;
	}
	time_duration_ns delta_ns = (last_gyro_timestamp_ == 0) ? 1e6 : timestamp - last_gyro_timestamp_;
	if (delta_ns > 1e10) {

		SIMPLE_IMU_DEBUG("Clamping integration period");
		// Limit integration to 1/10th of a second
		// Does not affect updating the last gyro timestamp.
		delta_ns = 1e10;
	}
	float dt = time_ns_to_s(delta_ns);
	last_gyro_timestamp_ = timestamp;
	Eigen::Vector3d incRot = gyro * dt;
	// Crude handling of "approximately zero"
	if (incRot.squaredNorm() < gyro_min_squared_length_) {
		SIMPLE_IMU_TRACE("Discarding gyro data that is approximately zero");
		return false;
	}

	angVel_ = gyro;

	// Update orientation
	quat_ = quat_ * flexkalman::util::quat_exp(incRot * 0.5);

	return true;
}
inline bool
SimpleIMUFusion::handleAccel(Eigen::Vector3d const &accel, timepoint_ns timestamp)
{
	uint64_t delta_ns = (last_accel_timestamp_ == 0) ? 1e6 : timestamp - last_accel_timestamp_;
	float dt = time_ns_to_s(delta_ns);
	if (!started_) {
		auto diff = std::abs(accel.norm() - MATH_GRAVITY_M_S2);
		if (diff > 1.) {
			// We're moving, don't start it now.

			SIMPLE_IMU_DEBUG(
			    "Can't start tracker with this accel "
			    "sample: we're moving too much.");
			return false;
		}

		// Initially, just set it to totally trust gravity.
		started_ = true;
		quat_ = Eigen::Quaterniond::FromTwoVectors(accel.normalized(), Eigen::Vector3d::UnitY());
		accel_filter_.addSample(accel, timestamp);
		gravity_filter_.addSample(accel.norm(), timestamp);
		last_accel_timestamp_ = timestamp;

		SIMPLE_IMU_DEBUG("Got a usable startup accel report");
		return true;
	}
	last_accel_timestamp_ = timestamp;
	accel_filter_.addSample(accel, timestamp);
	gravity_filter_.addSample(accel.norm(), timestamp);

	// Adjust scale of accelerometer
	Eigen::Vector3d adjusted_accel = accel_filter_.getState() * getAccelScaleFactor_();

	// How different is the acceleration length from gravity?
	auto diff = std::abs(adjusted_accel.norm() - MATH_GRAVITY_M_S2);
	auto scale = 1. - diff;
	if (scale <= 0) {
		// Too far from gravity to be useful/trusted for orientation
		// purposes.
		SIMPLE_IMU_TRACE("Too far from gravity to be useful/trusted.");
		return false;
	}

	// This should match the global gravity vector if the rotation
	// is right.
	Eigen::Vector3d measuredGravityDirection = (quat_ * adjusted_accel).normalized();
	auto incremental = Eigen::Quaterniond::FromTwoVectors(measuredGravityDirection, Eigen::Vector3d::UnitY());

	double alpha = scale * gravity_scale_ * dt;
	Eigen::Quaterniond scaledIncrementalQuat = Eigen::Quaterniond::Identity().slerp(alpha, incremental);

	// Update orientation
	quat_ = scaledIncrementalQuat * quat_;

	return true;
}

} // namespace xrt_fusion
