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

#include "t_lowpass.h"
#include "t_lowpass_vector.h"
#include "math/m_api.h"
#include "util/u_time.h"

#include <Eigen/Core>
#include <Eigen/Geometry>

#include "flexkalman/EigenQuatExponentialMap.h"

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
	    : gravity_scale_(gravity_rate)
	{}

	bool
	valid() const noexcept
	{
		return started_;
	}

	Eigen::Quaterniond
	getQuat() const
	{
		return quat_;
	}

	Eigen::Quaterniond
	getPredictedQuat(timepoint_ns timestamp) const
	{
		timepoint_ns state_time =
		    std::max(last_accel_timestamp_, last_gyro_timestamp_);
		time_duration_ns delta_ns =
		    (state_time == 0) ? 1e6 : timestamp - state_time;
		float dt = time_ns_to_s(delta_ns);
		return quat_ * flexkalman::util::quat_exp(angVel_ * dt * 0.5);
	}

	Eigen::Vector3d
	getRotationVec() const
	{
		return flexkalman::util::quat_ln(quat_);
	}

	//! in world space
	Eigen::Vector3d const &
	getAngVel() const
	{
		return angVel_;
	}

	bool
	handleGyro(Eigen::Vector3d const &gyro, timepoint_ns timestamp)
	{
		if (!started_) {
			return false;
		}
		time_duration_ns delta_ns =
		    (last_gyro_timestamp_ == 0)
		        ? 1e6
		        : timestamp - last_gyro_timestamp_;
		if (delta_ns > 1e10) {
			// Limit integration to 1/10th of a second
			// Does not affect updating the last gyro timestamp.
			delta_ns = 1e10;
		}
		float dt = time_ns_to_s(delta_ns);
		last_gyro_timestamp_ = timestamp;
		Eigen::Vector3d incRot = gyro * dt;
		// Crude handling of "approximately zero"
		if (incRot.squaredNorm() < 1.e-8) {
			return false;
		}

		angVel_ = gyro;

		// Update orientation
		quat_ = quat_ * flexkalman::util::quat_exp(incRot * 0.5);

		return true;
	}

	/*!
	 * Returns a coefficient to correct the scale of the accelerometer
	 * reading.
	 */
	double
	getAccelScaleFactor() const
	{
		return MATH_GRAVITY_M_S2 / gravity_filter_.getState();
	}

	bool
	handleAccel(Eigen::Vector3d const &accel, timepoint_ns timestamp)
	{
		uint64_t delta_ns = (last_accel_timestamp_ == 0)
		                        ? 1e6
		                        : timestamp - last_accel_timestamp_;
		float dt = time_ns_to_s(delta_ns);
		if (!started_) {
			auto diff = std::abs(accel.norm() - MATH_GRAVITY_M_S2);
			if (diff > 1.) {
				// We're moving, don't start it now.
				return false;
			}

			// Initially, just set it to totally trust gravity.
			started_ = true;
			quat_ = Eigen::Quaterniond::FromTwoVectors(
			    accel.normalized(), Eigen::Vector3d::UnitY());
			accel_filter_.addSample(accel, timestamp);
			gravity_filter_.addSample(accel.norm(), timestamp);
			last_accel_timestamp_ = timestamp;
			return true;
		}
		last_accel_timestamp_ = timestamp;
		accel_filter_.addSample(accel, timestamp);
		gravity_filter_.addSample(accel.norm(), timestamp);

		// Adjust scale of accelerometer
		Eigen::Vector3d adjusted_accel =
		    accel_filter_.getState() * getAccelScaleFactor();
		auto diff = std::abs(adjusted_accel.norm() - MATH_GRAVITY_M_S2);
		auto scale = 1. - diff;
		if (scale <= 0) {
			// Too far from gravity to be useful/trusted.
			return false;
		}

		// This should match the global gravity vector if the rotation
		// is right.
		Eigen::Vector3d measuredGravityDirection =
		    (quat_ * adjusted_accel).normalized();
		auto incremental = Eigen::Quaterniond::FromTwoVectors(
		    measuredGravityDirection, Eigen::Vector3d::UnitY());

		double alpha = scale * gravity_scale_ * dt;
		Eigen::Quaterniond scaledIncrementalQuat =
		    Eigen::Quaterniond::Identity().slerp(alpha, incremental);

		// Update orientation
		quat_ = scaledIncrementalQuat * quat_;

		return true;
	}

	/*!
	 * Use this to obtain the residual, world-space acceleration not
	 * associated with gravity, after incorporating a measurement.
	 */
	Eigen::Vector3d
	getCorrectedWorldAccel(Eigen::Vector3d const &accel) const
	{
		Eigen::Vector3d adjusted_accel = accel * getAccelScaleFactor();
		return (quat_ * adjusted_accel) -
		       (Eigen::Vector3d::UnitY() * MATH_GRAVITY_M_S2);
	}

	Eigen::Matrix3d
	getRotationMatrix() const
	{
		return quat_.toRotationMatrix();
	}

	void
	postCorrect()
	{
		quat_.normalize();
	}

private:
	Eigen::Vector3d angVel_{Eigen::Vector3d::Zero()};
	Eigen::Quaterniond quat_{Eigen::Quaterniond::Identity()};
	double gravity_scale_;
	LowPassIIRVectorFilter<3, double> accel_filter_{
	    200 /* hz cutoff frequency */};
	LowPassIIRFilter<double> gravity_filter_{1 /* hz cutoff frequency */};
	uint64_t last_accel_timestamp_{0};
	uint64_t last_gyro_timestamp_{0};
	bool started_{false};
};
} // namespace xrt_fusion
