// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Low-pass IIR filter
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_tracking
 */

#pragma once

#ifndef __cplusplus
#error "This header is C++-only."
#endif

#include "util/u_time.h"

#include "math/m_mathinclude.h"
#include <cmath>
#include <type_traits>


namespace xrt_fusion {
namespace implementation {
	/*!
	 * The shared implementation (between vector and scalar versions) of an
	 * IIR low-pass filter.
	 */
	template <typename Value, typename Scalar> struct LowPassIIR
	{
		// For fixed point, you'd need more bits of data storage. See
		// https://www.embeddedrelated.com/showarticle/779.php
		static_assert(std::is_floating_point<Scalar>::value,
		              "Filter is designed only for floating-point values. If "
		              "you want fixed-point, you must reimplement it.");
		// EIGEN_MAKE_ALIGNED_OPERATOR_NEW

		/*!
		 * Constructor
		 *
		 * @param cutoff_hz A cutoff frequency in Hertz: signal changes
		 * much lower in frequency will be passed through the filter,
		 * while signal changes much higher in frequency will be
		 * blocked.
		 *
		 * @param val The value to initialize the filter with. Does not
		 * affect the filter itself: only seen if you access state
		 * before initializing the filter with the first sample.
		 */
		explicit LowPassIIR(Scalar cutoff_hz, Value const &val) noexcept
		    : state(val), time_constant(1.f / (2.f * M_PI * cutoff_hz))
		{}

		/*!
		 * Reset the filter to just-created state.
		 */
		void
		reset(Value const &val) noexcept
		{
			state = val;
			initialized = false;
			filter_timestamp_ns = 0;
		}

		/*!
		 * Filter a sample, with an optional weight.
		 *
		 * @param sample The value to filter
		 * @param timestamp_ns The time that this sample was measured.
		 * @param weight An optional value between 0 and 1. The smaller
		 * this value, the less the current sample influences the filter
		 * state. For the first call, this is always assumed to be 1.
		 */
		void
		addSample(Value const &sample, timepoint_ns timestamp_ns, Scalar weight = 1)
		{
			if (!initialized) {
				initialized = true;
				state = sample;
				filter_timestamp_ns = timestamp_ns;
				return;
			}
			// get dt in seconds
			Scalar dt = time_ns_to_s(timestamp_ns - filter_timestamp_ns);
			//! @todo limit max dt?
			Scalar weighted = dt * weight;
			Scalar alpha = weighted / (time_constant + weighted);

			// The update step below is equivalent to
			// state = state * (1 - alpha) + alpha * sample;
			// -- it blends the current sample and the filter state
			// using alpha as the blending parameter.
			state += alpha * (sample - state);
			filter_timestamp_ns = timestamp_ns;
		}

		Value state;
		Scalar time_constant;
		bool initialized{false};
		timepoint_ns filter_timestamp_ns{0};
	};
} // namespace implementation

/*!
 * A very simple low-pass filter, using a "one-pole infinite impulse response"
 * design (one-pole IIR).
 *
 * Configurable in scalar type.
 */
template <typename Scalar> class LowPassIIRFilter
{
public:
	/*!
	 * Constructor
	 *
	 * @param cutoff_hz A cutoff frequency in Hertz: signal changes much
	 * lower in frequency will be passed through the filter, while signal
	 * changes much higher in frequency will be blocked.
	 */
	explicit LowPassIIRFilter(Scalar cutoff_hz) noexcept : impl_(cutoff_hz, 0) {}


	/*!
	 * Reset the filter to just-created state.
	 */
	void
	reset() noexcept
	{
		impl_.reset(0);
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
	addSample(Scalar sample, timepoint_ns timestamp_ns, Scalar weight = 1)
	{
		impl_.addSample(sample, timestamp_ns, weight);
	}

	/*!
	 * Access the filtered value.
	 */
	Scalar
	getState() const noexcept
	{
		return impl_.state;
	}

	/*!
	 * Access the time of last update.
	 */
	timepoint_ns
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
	implementation::LowPassIIR<Scalar, Scalar> impl_;
};

} // namespace xrt_fusion
