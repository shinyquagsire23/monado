// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Low-pass IIR filter on vectors
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_tracking
 */

#pragma once

#ifndef __cplusplus
#error "This header is C++-only."
#endif

#include "tracking/t_lowpass.hpp"

#include <Eigen/Core>


namespace xrt_fusion {

/*!
 * A very simple low-pass filter, using a "one-pole infinite impulse response"
 * design (one-pole IIR).
 *
 * Configurable in dimension and scalar type.
 */
template <size_t Dim, typename Scalar> class LowPassIIRVectorFilter
{
public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	using Vector = Eigen::Matrix<Scalar, Dim, 1>;

	/*!
	 * Constructor
	 *
	 * @param cutoff_hz A cutoff frequency in Hertz: signal changes much
	 * lower in frequency will be passed through the filter, while signal
	 * changes much higher in frequency will be blocked.
	 */
	explicit LowPassIIRVectorFilter(Scalar cutoff_hz) noexcept : impl_(cutoff_hz, Vector::Zero()) {}


	/*!
	 * Reset the filter to just-created state.
	 */
	void
	reset() noexcept
	{
		impl_.reset(Vector::Zero());
	}

	/*!
	 * Filter a sample, with an optional weight.
	 *
	 * @param sample The value to filter
	 * @param timestamp_ns The time that this sample was measured.
	 * @param weight An optional value between 0 and 1. The smaller this
	 * value, the less the current sample influences the filter state. For
	 * the first call, this is always assumed to be 1.
	 */
	void
	addSample(Vector const &sample, std::uint64_t timestamp_ns, Scalar weight = 1)
	{
		impl_.addSample(sample, timestamp_ns, weight);
	}

	/*!
	 * Access the filtered value.
	 */
	Vector const &
	getState() const noexcept
	{
		return impl_.state;
	}

	/*!
	 * Access the time of last update.
	 */
	std::uint64_t
	getTimestampNs() const noexcept
	{
		return impl_.filter_timestamp_ns;
	}

	/*!
	 * Access whether we have initialized state.
	 */
	bool
	isInitialized() const noexcept
	{
		return impl_.initialized;
	}

private:
	implementation::LowPassIIR<Vector, Scalar> impl_;
};

} // namespace xrt_fusion
