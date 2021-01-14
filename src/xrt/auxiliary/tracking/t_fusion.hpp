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

#include <Eigen/Core>
#include <Eigen/Geometry>

#include "flexkalman/AugmentedProcessModel.h"
#include "flexkalman/AugmentedState.h"
#include "flexkalman/BaseTypes.h"
#include "flexkalman/PoseState.h"


namespace xrt_fusion {
namespace types = flexkalman::types;
using flexkalman::types::Vector;

//! For things like accelerometers, which on some level measure the local vector
//! of a world direction.
template <typename State>
class WorldDirectionMeasurement : public flexkalman::MeasurementBase<WorldDirectionMeasurement<State>>
{
public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	static constexpr size_t Dimension = 3;
	using MeasurementVector = types::Vector<Dimension>;
	using MeasurementSquareMatrix = types::SquareMatrix<Dimension>;
	WorldDirectionMeasurement(types::Vector<3> const &direction,
	                          types::Vector<3> const &reference,
	                          types::Vector<3> const &variance)
	    : direction_(direction.normalized()), reference_(reference.normalized()), covariance_(variance.asDiagonal())
	{}

	MeasurementSquareMatrix const &
	getCovariance(State const & /*s*/)
	{
		return covariance_;
	}

	types::Vector<3>
	predictMeasurement(State const &s) const
	{
		return s.getCombinedQuaternion() * reference_;
	}

	MeasurementVector
	getResidual(MeasurementVector const &predictedMeasurement, State const &s) const
	{
		return predictedMeasurement - reference_;
	}

	MeasurementVector
	getResidual(State const &s) const
	{
		return getResidual(predictMeasurement(s), s);
	}

private:
	types::Vector<3> direction_;
	types::Vector<3> reference_;
	MeasurementSquareMatrix covariance_;
};
#if 0
//! For things like accelerometers, which on some level measure the local vector
//! of a world direction.
class LinAccelWithGravityMeasurement
    : public flexkalman::MeasurementBase<LinAccelWithGravityMeasurement>
{
public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	static constexpr size_t Dimension = 3;
	using MeasurementVector = types::Vector<Dimension>;
	using MeasurementSquareMatrix = types::SquareMatrix<Dimension>;
	LinAccelWithGravityMeasurement(types::Vector<3> const &direction,
	                          types::Vector<3> const &reference,
	                          types::Vector<3> const &variance)
	    : direction_(direction), reference_(reference),
	      covariance_(variance.asDiagonal())
	{}

	// template <typename State>
	MeasurementSquareMatrix const &
	getCovariance(State const & /*s*/)
	{
		return covariance_;
	}

	// template <typename State>
	types::Vector<3>
	predictMeasurement(State const &s) const
	{
		return reference_;
	}

	// template <typename State>
	MeasurementVector
	getResidual(MeasurementVector const &predictedMeasurement,
	            State const &s) const
	{
		s.getQuaternion().conjugate() *
		        predictedMeasurement return predictedMeasurement -
		    reference_.normalized();
	}

	template <typename State>
	MeasurementVector
	getResidual(State const &s) const
	{
		MeasurementVector residual =
		    direction_ - reference_ * s.getQuaternion();
		return getResidual(predictMeasurement(s), s);
	}

private:
	types::Vector<3> direction_;
	types::Vector<3> reference_;
	MeasurementSquareMatrix covariance_;
};
#endif

class BiasedGyroMeasurement : public flexkalman::MeasurementBase<BiasedGyroMeasurement>
{
public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	static constexpr size_t Dimension = 3;
	using MeasurementVector = types::Vector<Dimension>;
	using MeasurementSquareMatrix = types::SquareMatrix<Dimension>;
	BiasedGyroMeasurement(types::Vector<3> const &angVel, types::Vector<3> const &variance)
	    : angVel_(angVel), covariance_(variance.asDiagonal())
	{}

	template <typename State>
	MeasurementSquareMatrix const &
	getCovariance(State const & /*s*/)
	{
		return covariance_;
	}

	template <typename State>
	types::Vector<3>
	predictMeasurement(State const &s) const
	{
		return s.b().stateVector() + angVel_;
	}

	template <typename State>
	MeasurementVector
	getResidual(MeasurementVector const &predictedMeasurement, State const &s) const
	{
		return predictedMeasurement - s.a().angularVelocity();
	}

	template <typename State>
	MeasurementVector
	getResidual(State const &s) const
	{
		return getResidual(predictMeasurement(s), s);
	}

private:
	types::Vector<3> angVel_;
	MeasurementSquareMatrix covariance_;
};
/*!
 * For PS Move-like things, where there's a directly-computed absolute position
 * that is not at the tracked body's origin.
 */
class AbsolutePositionLeverArmMeasurement : public flexkalman::MeasurementBase<AbsolutePositionLeverArmMeasurement>
{
public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	using State = flexkalman::pose_externalized_rotation::State;
	static constexpr size_t Dimension = 3;
	using MeasurementVector = types::Vector<Dimension>;
	using MeasurementSquareMatrix = types::SquareMatrix<Dimension>;

	/*!
	 * @todo the point we get from the camera isn't the center of the ball,
	 * but the center of the visible surface of the ball - a closer
	 * approximation would be translation along the vector to the center of
	 * projection....
	 */
	AbsolutePositionLeverArmMeasurement(MeasurementVector const &measurement,
	                                    MeasurementVector const &knownLocationInBodySpace,
	                                    MeasurementVector const &variance)
	    : measurement_(measurement), knownLocationInBodySpace_(knownLocationInBodySpace),
	      covariance_(variance.asDiagonal())
	{}

	MeasurementSquareMatrix const &
	getCovariance(State const & /*s*/)
	{
		return covariance_;
	}

	types::Vector<3>
	predictMeasurement(State const &s) const
	{
		return s.getIsometry() * knownLocationInBodySpace_;
	}

	MeasurementVector
	getResidual(MeasurementVector const &predictedMeasurement, State const & /*s*/) const
	{
		return measurement_ - predictedMeasurement;
	}

	MeasurementVector
	getResidual(State const &s) const
	{
		return getResidual(predictMeasurement(s), s);
	}

private:
	MeasurementVector measurement_;
	MeasurementVector knownLocationInBodySpace_;
	MeasurementSquareMatrix covariance_;
};
} // namespace xrt_fusion
