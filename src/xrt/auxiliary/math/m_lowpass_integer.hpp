// Copyright 2019, 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Low-pass IIR filter for integers
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#ifndef __cplusplus
#error "This header is C++-only."
#endif

#include "util/u_time.h"

#include "math/m_mathinclude.h"
#include "math/m_rational.hpp"
#include <cmath>
#include <type_traits>
#include <stdexcept>
#include <cassert>

namespace xrt::auxiliary::math {


namespace detail {
	/*!
	 * The shared implementation (between vector and scalar versions) of an integer
	 * IIR/exponential low-pass filter.
	 */
	template <typename Value, typename Scalar> struct IntegerLowPassIIR
	{
		// For fixed point, you'd need more bits of data storage. See
		// https://www.embeddedrelated.com/showarticle/779.php
		static_assert(std::is_integral<Scalar>::value,
		              "Filter is designed only for integral values. Use the other one for floats.");

		/*!
		 * Constructor
		 *
		 * @param alpha_ The alpha value used to blend between new input and existing state. Larger values mean
		 * more influence from new input. @p alpha_.isBetweenZeroAndOne() must be true.
		 *
		 * @param val The value to initialize the filter with. Does not
		 * affect the filter itself: only seen if you get the state
		 * before initializing the filter with the first sample.
		 */
		explicit IntegerLowPassIIR(math::Rational<Scalar> alpha_, Value const &val)
		    : state(val), alpha(alpha_.withNonNegativeDenominator())
		{
			if (!alpha.isBetweenZeroAndOne()) {
				throw std::invalid_argument("Alpha must be between zero and one.");
			}
		}

		/*!
		 * Reset the filter to newly-created state.
		 */
		void
		reset(Value const &val) noexcept
		{
			state = val;
			initialized = false;
		}

		/*!
		 * Filter a sample, with an optional weight.
		 *
		 * @param sample The value to filter
		 * @param weight An optional value between 0 and 1. The smaller
		 * this value, the less the current sample influences the filter
		 * state. For the first call, this is always assumed to be 1.
		 */
		void
		addSample(Value const &sample, math::Rational<Scalar> weight = math::Rational<Scalar>::simplestUnity())
		{
			if (!initialized) {
				initialized = true;
				state = sample;
				return;
			}
			math::Rational<Scalar> weightedAlpha = alpha * weight;

			math::Rational<Scalar> oneMinusWeightedAlpha = weightedAlpha.complement();

			Value scaledStateNumerator = oneMinusWeightedAlpha.numerator * state;
			Value scaledSampleNumerator = weightedAlpha.numerator * sample;

			// can't use the re-arranged update from the float impl because we might be unsigned.
			state = (scaledStateNumerator + scaledSampleNumerator) / weightedAlpha.denominator;
		}

		Value state;
		math::Rational<Scalar> alpha;
		bool initialized{false};
	};
} // namespace detail

/*!
 * A very simple integer low-pass filter, using a "one-pole infinite impulse response"
 * design (one-pole IIR), also known as an exponential filter.
 *
 * Configurable in scalar type.
 */
template <typename Scalar> class IntegerLowPassIIRFilter
{
public:
	/*!
	 * Constructor
	 *
	 * @note Taking alpha, not a cutoff frequency, here, because it's easier with the rational math.
	 *
	 * @param alpha The alpha value used to blend between new input and existing state. Larger values mean
	 * more influence from new input. @ref math::Rational::isBetweenZeroAndOne() must be true for @p alpha.
	 */
	explicit IntegerLowPassIIRFilter(math::Rational<Scalar> alpha) noexcept : impl_(alpha, 0)
	{
		assert(alpha.isBetweenZeroAndOne());
	}

	/*!
	 * Reset the filter to newly-created state.
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
	 * @param weight An optional value between 0 and 1. The smaller this
	 * value, the less the current sample influences the filter state. For
	 * the first call, this is always assumed to be 1 regardless of what you pass.
	 */
	void
	addSample(Scalar sample, math::Rational<Scalar> weight = math::Rational<Scalar>::simplestUnity())
	{
		impl_.addSample(sample, weight);
	}

	/*!
	 * Get the filtered value.
	 */
	Scalar
	getState() const noexcept
	{
		return impl_.state;
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
	detail::IntegerLowPassIIR<Scalar, Scalar> impl_;
};

} // namespace xrt::auxiliary::math
