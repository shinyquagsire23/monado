// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A very simple rational number type.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_math
 */

#pragma once

#ifndef __cplusplus
#error "This header is C++-only."
#endif

#include <type_traits>
#include <limits>
#include <stdexcept>

namespace xrt::auxiliary::math {

/*!
 * A rational (fractional) number type.
 */
template <typename Scalar> struct Rational
{
	static_assert(std::is_integral_v<Scalar>, "This type is only for use with integer components.");

	using value_type = Scalar;
	value_type numerator;
	value_type denominator;

	/*!
	 * Return the rational value 1/1, the simplest unity (== 1) value.
	 */
	static constexpr Rational<Scalar>
	simplestUnity() noexcept
	{
		return {1, 1};
	}

	/*!
	 * Return the reciprocal of this value.
	 *
	 * Result will have a non-negative denominator.
	 */
	constexpr Rational
	reciprocal() const noexcept
	{
		return Rational{denominator, numerator}.withNonNegativeDenominator();
	}

	/*!
	 * Return this value, with the denominator non-negative (0 or positive).
	 */
	constexpr Rational
	withNonNegativeDenominator() const noexcept
	{
		if constexpr (std::is_unsigned_v<Scalar>) {
			// unsigned means always non-negative
			return *this;
		} else {
			return denominator < Scalar{0} ? Rational{-numerator, -denominator} : *this;
		}
	}

	/*!
	 * Does this rational number represent a value greater than 1, with a positive denominator?
	 *
	 */
	constexpr bool
	isOverUnity() const noexcept
	{
		return numerator > denominator && denominator > Scalar{0};
	}

	/*!
	 * Does this rational number represent 1?
	 *
	 * @note false if denominator is 0, even if numerator is also 0.
	 */
	constexpr bool
	isUnity() const noexcept
	{
		return numerator == denominator && denominator != Scalar{0};
	}

	/*!
	 * Does this rational number represent 0?
	 *
	 * @note false if denominator is 0, even if numerator is also 0.
	 */
	constexpr bool
	isZero() const noexcept
	{
		return numerator == Scalar{0} && denominator != Scalar{0};
	}

	/*!
	 * Does this rational number represent a value between 0 and 1 (exclusive), and has a positive denominator?
	 *
	 * This is the most common useful range.
	 */
	constexpr bool
	isBetweenZeroAndOne() const noexcept
	{
		return denominator > Scalar{0} && numerator > Scalar{0} && numerator < denominator;
	}

	/*!
	 * Get the complementary fraction.
	 *
	 * Only really makes sense if isBetweenZeroAndOne() is true
	 *
	 * Result will have a non-negative denominator.
	 */
	constexpr Rational
	complement() const noexcept
	{
		return Rational{denominator - numerator, denominator}.withNonNegativeDenominator();
	}
};

/*!
 * Multiplication operator. Warning: does no simplification!
 *
 * Result will have a non-negative denominator.
 *
 * @relates Rational
 */
template <typename Scalar>
constexpr Rational<Scalar>
operator*(const Rational<Scalar> &lhs, const Rational<Scalar> &rhs)
{
	return Rational<Scalar>{lhs.numerator * rhs.numerator, lhs.denominator * rhs.denominator}
	    .withNonNegativeDenominator();
}

/*!
 * Multiplication operator with a scalar. Warning: does no simplification!
 *
 * Result will have a non-negative denominator.
 *
 * @relates Rational
 */
template <typename Scalar>
constexpr Rational<Scalar>
operator*(const Rational<Scalar> &lhs, const Scalar &rhs)
{
	return Rational<Scalar>{lhs.numerator * rhs, lhs.denominator}.withNonNegativeDenominator();
}

/*!
 * Multiplication operator with a scalar. Warning: does no simplification!
 *
 * Result will have a non-negative denominator.
 *
 * @relates Rational
 */
template <typename Scalar>
constexpr Rational<Scalar>
operator*(const Scalar &lhs, const Rational<Scalar> &rhs)
{
	return (rhs * lhs).withNonNegativeDenominator();
}

/*!
 * Equality comparison operator. Warning: does no simplification, looks for exact equality!
 *
 * @relates Rational
 */
template <typename Scalar>
constexpr bool
operator==(const Rational<Scalar> &lhs, const Rational<Scalar> &rhs)
{
	return rhs.numerator == lhs.numerator && rhs.denominator == lhs.denominator;
}

/*!
 * Division operator. Warning: does no simplification!
 *
 * Result will have a non-negative denominator.
 *
 * @relates Rational
 */
template <typename Scalar>
constexpr Rational<Scalar>
operator/(const Rational<Scalar> &lhs, const Rational<Scalar> &rhs)
{
	return (lhs * rhs.reciprocal()).withNonNegativeDenominator();
}


/*!
 * Division operator by a scalar. Warning: does no simplification!
 *
 * Result will have a non-negative denominator.
 *
 * @relates Rational
 */
template <typename Scalar>
constexpr Rational<Scalar>
operator/(const Rational<Scalar> &lhs, Scalar rhs)
{
	return (lhs * Rational<Scalar>{1, rhs});
}


} // namespace xrt::auxiliary::math
