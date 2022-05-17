// Copyright 2021-2022, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 *
 * @brief A template class to serve as the base of iterator and const_iterator
 * types for things with "random access".
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <stdexcept>
#include <limits>
#include <iterator>

namespace xrt::auxiliary::util {
/*!
 * @brief Template for base class used by "random-access" iterators and const_iterators, providing all the functionality
 * that is independent of element type and const-ness of the iterator.
 *
 * Using inheritance instead of composition here to more easily satisfy the C++ standard's requirements for
 * iterators (e.g. that const iterators and iterators are comparable, etc.)
 *
 * All invalid instances will compare as equal, as required by the spec, but they are not all equivalent. You can freely
 * go "past the end" (they will be invalid, so cannot dereference, but you can get them back to valid), but you can't go
 * "past the beginning". That is, you can do `*(buf.end() - 1)` successfully if your buffer has at least one element,
 * even though `buf.end()` is invalid.
 *
 * @tparam ContainerOrHelper Your container or some member thereof that provides a size() method. If it's a helper
 * instead of the actual container, make it const.
 *
 */
template <typename ContainerOrHelper> class RandomAccessIteratorBase
{
public:
	using iterator_category = std::random_access_iterator_tag;
	using difference_type = std::ptrdiff_t;


	//! Is this iterator valid?
	bool
	valid() const noexcept;

	//! Is this iterator valid?
	explicit operator bool() const noexcept
	{
		return valid();
	}

	//! What is the index stored by this iterator?
	size_t
	index() const noexcept
	{
		return index_;
	}

	//! Is this iterator pointing "past the end" of the container?
	bool
	is_past_the_end() const noexcept
	{
		return container_ != nullptr && index_ >= container_->size();
	}

	/*!
	 * @brief True if this iterator is "irrecoverably" invalid (that is, cleared or default constructed).
	 *
	 * Implies !valid() but is stronger. `buf.end().is_cleared()` is false.
	 */
	bool
	is_cleared() const noexcept
	{
		return container_ == nullptr;
	}

	/*!
	 * @brief Compute the difference between two iterators.
	 *
	 * - If both are cleared, the result is 0.
	 * - Otherwise the result is the difference in index.
	 *
	 * @throws std::logic_error if exactly one of the iterators is cleared
	 * @throws std::out_of_range if at least one of the iterators has an index larger than the maximum value of
	 * std::ptrdiff_t.
	 */
	std::ptrdiff_t
	operator-(const RandomAccessIteratorBase<ContainerOrHelper> &other) const;

	/*!
	 * @brief Increment by an arbitrary value.
	 */
	RandomAccessIteratorBase &
	operator+=(std::ptrdiff_t n);

	/*!
	 * @brief Decrement by an arbitrary value.
	 */
	RandomAccessIteratorBase &
	operator-=(std::ptrdiff_t n);

	/*!
	 * @brief Add some arbitrary amount to a copy of this iterator.
	 */
	RandomAccessIteratorBase
	operator+(std::ptrdiff_t n) const;

	/*!
	 * @brief Subtract some arbitrary amount from a copy of this iterator.
	 */
	RandomAccessIteratorBase
	operator-(std::ptrdiff_t n) const;

	//! Factory function: construct the "begin" iterator
	static RandomAccessIteratorBase
	begin(ContainerOrHelper &container)
	{
		return RandomAccessIteratorBase(container, 0);
	}

	//! Factory function: construct the "past the end" iterator that can be decremented safely
	static RandomAccessIteratorBase
	end(ContainerOrHelper &container)
	{
		return RandomAccessIteratorBase(container, container.size());
	}


	/**
	 * @brief Default constructor - initializes to "cleared" (that is, irrecoverably invalid - because we have no
	 * reference to a container)
	 */
	RandomAccessIteratorBase() = default;

	/**
	 * @brief Constructor from a helper/container and index
	 *
	 * @param container The helper or container we will iterate through.
	 * @param index An index - may be out of range.
	 */
	explicit RandomAccessIteratorBase(ContainerOrHelper &container, size_t index);


	using const_iterator_base = std::conditional_t<std::is_const<ContainerOrHelper>::value,
	                                               RandomAccessIteratorBase,
	                                               RandomAccessIteratorBase<std::add_const_t<ContainerOrHelper>>>;

	/**
	 * @brief Get a const iterator base pointing to the same element as this element.
	 *
	 * @return const_iterator_base
	 */
	const_iterator_base
	as_const() const
	{
		if (is_cleared()) {
			return {};
		}

		return const_iterator_base{*container_, index_};
	}

protected:
	//! for internal use
	RandomAccessIteratorBase(ContainerOrHelper *container, size_t index) : container_(container), index_(index) {}

	//! Increment an arbitrary amount
	void
	increment_n(std::size_t n);

	//! Decrement an arbitrary amount
	void
	decrement_n(std::size_t n);

	//! Get the associated container or helper
	ContainerOrHelper *
	container() const noexcept
	{
		return container_;
	}

private:
	/**
	 * @brief The container or helper we're associated with.
	 *
	 * If we were created knowing a container, this pointer is non-null.
	 * Used to determine if an index is in bounds.
	 * If this is null, the iterator is irrecoverably invalid.
	 */
	ContainerOrHelper *container_{nullptr};

	//! This is the index in the container. May be out-of-range.
	size_t index_{0};
};

/*!
 * @brief Equality comparison operator for @ref RandomAccessIteratorBase
 *
 * @relates RandomAccessIteratorBase
 */
template <typename ContainerOrHelper>
static inline bool
operator==(RandomAccessIteratorBase<ContainerOrHelper> const &lhs,
           RandomAccessIteratorBase<ContainerOrHelper> const &rhs) noexcept
{
	const bool lhs_valid = lhs.valid();
	const bool rhs_valid = rhs.valid();
	if (!lhs_valid && !rhs_valid) {
		// all invalid iterators compare equal.
		return true;
	}
	if (lhs_valid != rhs_valid) {
		// valid is never equal to invalid.
		return false;
	}
	// OK, so both are valid. Now, we look at the index
	return lhs.index() == rhs.index();
}

/*!
 * @overload
 */
template <typename ContainerOrHelper>
static inline bool
operator==(RandomAccessIteratorBase<const ContainerOrHelper> const &lhs,
           RandomAccessIteratorBase<ContainerOrHelper> const &rhs) noexcept
{
	return lhs == rhs.as_const();
}
/*!
 * @overload
 */
template <typename ContainerOrHelper>
static inline bool
operator==(RandomAccessIteratorBase<ContainerOrHelper> const &lhs,
           RandomAccessIteratorBase<const ContainerOrHelper> const &rhs) noexcept
{
	return lhs.as_const() == rhs;
}

/*!
 * @brief Inequality comparison operator for @ref RandomAccessIteratorBase
 *
 * @relates RandomAccessIteratorBase
 */
template <typename ContainerOrHelper>
static inline bool
operator!=(RandomAccessIteratorBase<ContainerOrHelper> const &lhs,
           RandomAccessIteratorBase<ContainerOrHelper> const &rhs) noexcept
{
	return !(lhs == rhs);
}

/*!
 * @overload
 */
template <typename ContainerOrHelper>
static inline bool
operator!=(RandomAccessIteratorBase<const ContainerOrHelper> const &lhs,
           RandomAccessIteratorBase<ContainerOrHelper> const &rhs) noexcept
{
	return !(lhs == rhs.as_const());
}

/*!
 * @overload
 */
template <typename ContainerOrHelper>
static inline bool
operator!=(RandomAccessIteratorBase<ContainerOrHelper> const &lhs,
           RandomAccessIteratorBase<const ContainerOrHelper> const &rhs) noexcept
{
	return !(lhs.as_const() == rhs);
}

template <typename ContainerOrHelper>
inline bool
RandomAccessIteratorBase<ContainerOrHelper>::valid() const noexcept
{
	return container_ != nullptr && index_ < container_->size();
}

template <typename ContainerOrHelper>
inline RandomAccessIteratorBase<ContainerOrHelper> &
RandomAccessIteratorBase<ContainerOrHelper>::operator+=(std::ptrdiff_t n)
{
	if (n < 0) {
		decrement_n(static_cast<size_t>(-1 * n));
	} else {
		increment_n(static_cast<size_t>(n));
	}
	return *this;
}

template <typename ContainerOrHelper>
inline RandomAccessIteratorBase<ContainerOrHelper> &
RandomAccessIteratorBase<ContainerOrHelper>::operator-=(std::ptrdiff_t n)
{
	if (n < 0) {
		increment_n(static_cast<size_t>(-1 * n));
	} else {
		decrement_n(static_cast<size_t>(n));
	}
	return *this;
}

template <typename ContainerOrHelper>
inline void
RandomAccessIteratorBase<ContainerOrHelper>::increment_n(std::size_t n)
{
	// being cleared is permanent
	if (is_cleared())
		return;
	index_ += n;
}

template <typename ContainerOrHelper>
inline void
RandomAccessIteratorBase<ContainerOrHelper>::decrement_n(std::size_t n)
{
	// being cleared is permanent
	if (is_cleared())
		return;
	if (n > index_) {
		// would move backward past the beginning which you can't recover from. So, clear it.
		*this = {};
		return;
	}
	index_ -= n;
}

template <typename ContainerOrHelper>
inline std::ptrdiff_t
RandomAccessIteratorBase<ContainerOrHelper>::operator-(const RandomAccessIteratorBase<ContainerOrHelper> &other) const
{
	const bool self_cleared = is_cleared();
	const bool other_cleared = other.is_cleared();
	if (self_cleared && other_cleared) {
		// If both cleared, they're at the same place.
		return 0;
	}
	if (self_cleared || other_cleared) {
		// If only one is cleared, we can't do this.
		throw std::logic_error(
		    "Tried to find the difference between a cleared iterator and a non-cleared iterator.");
	}
	constexpr size_t max_ptrdiff = static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max());
	if (index_ > max_ptrdiff || other.index_ > max_ptrdiff) {
		throw std::out_of_range("An index exceeded the maximum value of the signed version of the index type.");
	}
	// Otherwise subtract the index values.
	return static_cast<std::ptrdiff_t>(index_) - static_cast<std::ptrdiff_t>(other.index_);
}

template <typename ContainerOrHelper>
inline RandomAccessIteratorBase<ContainerOrHelper>::RandomAccessIteratorBase(ContainerOrHelper &container, size_t index)
    : container_(&container), index_(index)
{}

} // namespace xrt::auxiliary::util
