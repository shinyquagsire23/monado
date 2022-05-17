// Copyright 2019, 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Low-pass IIR filter on vectors
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#ifndef __cplusplus
#error "This header is C++-only."
#endif

#include "math/m_lowpass_float.hpp"

#include <Eigen/Core>


namespace xrt::auxiliary::math {

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
	 * Reset the filter to newly-created state.
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
	 * Get the filtered value.
	 */
	Vector const &
	getState() const noexcept
	{
		return impl_.state;
	}

	/*!
	 * Get the time of last update.
	 */
	std::uint64_t
	getTimestampNs() const noexcept
	{
		return impl_.filter_timestamp_ns;
	}

	/*!
	 * Get whether we have initialized state.
	 */
	bool
	isInitialized() const noexcept
	{
		return impl_.initialized;
	}

private:
	detail::LowPassIIR<Vector, Scalar> impl_;
};

} // namespace xrt::auxiliary::math
