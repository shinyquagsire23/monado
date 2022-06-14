// Copyright 2021-2022, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief All the "element-type-independent" code (helper objects, base classes) for a ringbuffer implementation on top
 * of a fixed size array
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <algorithm>
#include <assert.h>
#include <stdlib.h>


//|  -4   |   -3   |  -2 | -1 | Top | Garbage |
// OR
//|  -4   |   -3   |  -2 | -1 | Top | -7 | -6 | -5 |

namespace xrt::auxiliary::util::detail {

/**
 * @brief All the bookkeeping for adapting a fixed-size array to a ring buffer.
 *
 * This is all the guts of the ring buffer except for the actual buffer.
 * We split it out to
 * - reduce code size (this can be shared among multiple types)
 * - separate concerns (keeping track of the indices separate from owning the buffer)
 * - allow easier implementation of both const iterators and non-const iterators
 *
 * There are a few types of "index":
 *
 * - just "index": an index where the least-recently-added element still remaining is numbered 0, the next
 *   oldest is 1, etc. (Chronological)
 * - "age": Reverse chronological order: 0 means most-recently-added, 1 means the one before it, etc.
 * - "inner" index: the index in the underlying array/buffer. It's called "inner" because the consumer of the
 *   ring buffer should not ever deal with this index, it's an implementation detail.
 */
class RingBufferHelper
{
public:
	//! Construct for a given size
	explicit constexpr RingBufferHelper(size_t capacity) : capacity_(capacity) {}
	RingBufferHelper(RingBufferHelper const &) = default;
	RingBufferHelper(RingBufferHelper &&) = default;
	RingBufferHelper &
	operator=(RingBufferHelper const &) = default;
	RingBufferHelper &
	operator=(RingBufferHelper &&) = default;

	//! Get the inner index for a given age (if possible)
	bool
	age_to_inner_index(size_t age, size_t &out_inner_idx) const noexcept;

	//! Get the inner index for a given age, clamping it if out of bounds
	bool
	clamped_age_to_inner_index(size_t age, size_t &out_inner_idx) const noexcept;

	//! Get the inner index for a given index (if possible)
	bool
	index_to_inner_index(size_t index, size_t &out_inner_idx) const noexcept;

	//! Is the buffer empty?
	bool
	empty() const noexcept
	{
		return length_ == 0;
	}

	//! How many elements are in the buffer?
	size_t
	size() const noexcept
	{
		return length_;
	}

	/*!
	 * @brief Update internal state for pushing an element to the back, and return the inner index to store
	 * the element at.
	 *
	 * This is the implementation of "push_back" excluding all the messy "actually dealing with the data"
	 * part ;-)
	 */
	size_t
	push_back_location() noexcept;

	/*!
	 * @brief Record the logical removal of the front element, if any.
	 *
	 * Does nothing if the buffer is empty. Does not actually modify the value stored in the backing array.
	 */
	void
	pop_front() noexcept;

	/*!
	 * @brief Record the logical removal of the back element, if any.
	 *
	 * Returns false if the buffer is empty. Does not actually modify the
	 * value stored in the backing array.
	 */
	bool
	pop_back() noexcept;

	//! Get the inner index of the front (oldest) value, or capacity_ if empty.
	size_t
	front_inner_index() const noexcept;

	//! Get the inner index of the back (newest) value, or capacity_ if empty.
	size_t
	back_inner_index() const noexcept;

	void
	clear();

private:
	// Would be const, but that would mess up our ability to copy/move containers using this.
	size_t capacity_;

	//! The inner index containing the most recently added element, if any
	size_t latest_inner_idx_ = 0;

	//! The number of elements populated.
	size_t length_ = 0;

	/**
	 * @brief Get the inner index of the front (oldest) value: assumes not empty!
	 *
	 * For internal use in this class only.
	 *
	 * @see front_inner_index() for the "safe" equivalent (that wraps this with error handling)
	 */
	size_t
	front_impl_() const noexcept;
};


inline void
RingBufferHelper::clear()
{
	this->latest_inner_idx_ = 0;
	this->length_ = 0;
}

inline size_t
RingBufferHelper::front_impl_() const noexcept
{
	assert(!empty());
	// length will not exceed capacity_, so this will not underflow
	return (latest_inner_idx_ + capacity_ - length_ + 1) % capacity_;
}

inline bool
RingBufferHelper::age_to_inner_index(size_t age, size_t &out_inner_idx) const noexcept
{

	if (empty()) {
		return false;
	}
	if (age >= length_) {
		return false;
	}
	// latest_inner_idx_ is the same as (latest_inner_idx_ + capacity_) % capacity_ so we add capacity_ to
	// prevent underflow with unsigned values
	out_inner_idx = (latest_inner_idx_ + capacity_ - age) % capacity_;
	return true;
}

inline bool
RingBufferHelper::clamped_age_to_inner_index(size_t age, size_t &out_inner_idx) const noexcept
{
	if (empty()) {
		return false;
	}
	return age_to_inner_index((std::min)(age, length_ - 1), out_inner_idx);
}

inline bool
RingBufferHelper::index_to_inner_index(size_t index, size_t &out_inner_idx) const noexcept
{

	if (empty()) {
		return false;
	}
	if (index >= length_) {
		return false;
	}
	// add to the front (oldest) index and take modulo capacity_
	out_inner_idx = (front_impl_() + index) % capacity_;
	return true;
}

inline size_t
RingBufferHelper::push_back_location() noexcept
{
	// We always increment the latest inner index modulo capacity_
	latest_inner_idx_ = (latest_inner_idx_ + 1) % capacity_;
	// Length cannot exceed capacity_. If it already was capacity_, that means we're overwriting something at
	// latest_inner_idx_
	length_ = std::min(length_ + 1, capacity_);
	return latest_inner_idx_;
}

inline void
RingBufferHelper::pop_front() noexcept
{
	if (!empty()) {
		length_--;
	}
}
inline bool
RingBufferHelper::pop_back() noexcept
{
	if (empty()) {
		return false;
	}
	// adding capacity before -1 to avoid overflow
	latest_inner_idx_ = (latest_inner_idx_ + capacity_ - 1) % capacity_;
	length_--;
	return true;
}
inline size_t
RingBufferHelper::front_inner_index() const noexcept
{
	if (empty()) {
		return capacity_;
	}
	return front_impl_();
}

inline size_t
RingBufferHelper::back_inner_index() const noexcept
{
	if (empty()) {
		return capacity_;
	}
	return latest_inner_idx_;
}

} // namespace xrt::auxiliary::util::detail
