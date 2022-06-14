// Copyright 2021-2022, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Ringbuffer implementation for keeping track of the past state of things
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Moses Turner <moses@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "u_template_historybuf_impl_helpers.hpp"
#include "u_iterator_base.hpp"

#include <limits>
#include <array>

namespace xrt::auxiliary::util {

namespace detail {
	template <typename T, size_t MaxSize> class HistoryBufConstIterator;
	template <typename T, size_t MaxSize> class HistoryBufIterator;
} // namespace detail

/*!
 * @brief Stores some number of values in a ring buffer, overwriting the earliest-pushed-remaining element if out of
 * room.
 *
 * Note this should only store value types, since there's no way to destroy elements other than overwriting them, and
 * all elements are default-initialized upon construction of the container.
 */
template <typename T, size_t MaxSize> class HistoryBuffer
{
public:
	//! Is the buffer empty?
	bool
	empty() const noexcept;

	//! How many elements are in the buffer?
	size_t
	size() const noexcept;


	/*!
	 * @brief Put something at the back, overwriting whatever was at the front if necessary.
	 *
	 * This is permitted to invalidate iterators. They won't be poisoned, but they will return something you don't
	 * expect.
	 */
	void
	push_back(const T &element);

	/*!
	 * @brief Logically remove the newest element from the buffer.
	 *
	 * This is permitted to invalidate iterators. They won't be poisoned,
	 * but they will return something you don't expect.
	 *
	 * @return true if there was something to pop.
	 */
	bool
	pop_back() noexcept
	{
		return helper_.pop_back();
	}

	/*!
	 * @brief Logically remove the oldest element from the buffer.
	 *
	 * The value still remains in the backing container until overwritten, but it isn't accessible anymore.
	 *
	 * This invalidates iterators. They won't be poisoned, but they will return something you don't expect.
	 */
	void
	pop_front() noexcept
	{
		helper_.pop_front();
	}

	/*!
	 * @brief Use a value at a given age, where age 0 is the most recent value, age 1 precedes it, etc.
	 * (reverse chronological order)
	 *
	 * Out of bounds accesses will return nullptr.
	 */
	T *
	get_at_age(size_t age) noexcept;

	//! @overload
	const T *
	get_at_age(size_t age) const noexcept;

	/*!
	 * @brief Like get_at_age() but values larger than the oldest age are clamped.
	 */
	T *
	get_at_clamped_age(size_t age) noexcept
	{
		size_t inner_index = 0;
		if (helper_.clamped_age_to_inner_index(age, inner_index)) {
			return &internalBuffer[inner_index];
		}
		return nullptr;
	}

	//! @overload
	const T *
	get_at_clamped_age(size_t age) const noexcept
	{
		size_t inner_index = 0;
		if (helper_.clamped_age_to_inner_index(age, inner_index)) {
			return &internalBuffer[inner_index];
		}
		return nullptr;
	}

	/*!
	 * @brief Use a value at a given index, where 0 is the least-recent value still stored, index 1 follows it,
	 * etc. (chronological order)
	 *
	 * Out of bounds accesses will return nullptr.
	 */
	T *
	get_at_index(size_t index) noexcept;

	//! @overload
	const T *
	get_at_index(size_t index) const noexcept;

	using iterator = detail::HistoryBufIterator<T, MaxSize>;
	using const_iterator = detail::HistoryBufConstIterator<T, MaxSize>;

	//! Get a const iterator for the oldest element.
	const_iterator
	cbegin() const noexcept;

	//! Get a "past the end" (past the newest) const iterator
	const_iterator
	cend() const noexcept;

	//! Get a const iterator for the oldest element.
	const_iterator
	begin() const noexcept;

	//! Get a "past the end" (past the newest) const iterator
	const_iterator
	end() const noexcept;

	//! Get an iterator for the oldest element.
	iterator
	begin() noexcept;

	//! Get a "past the end" (past the newest) iterator
	iterator
	end() noexcept;

	/*!
	 * @brief Gets a reference to the front (oldest) element in the buffer.
	 * @throws std::logic_error if buffer is empty
	 */
	T &
	front();

	//! @overload
	const T &
	front() const;

	/*!
	 * @brief Gets a reference to the back (newest) element in the buffer.
	 * @throws std::logic_error if buffer is empty
	 */
	T &
	back();

	//! @overload
	const T &
	back() const;

	void
	clear();

private:
	// Make sure all valid indices can be represented in a signed integer of the same size
	static_assert(MaxSize < (std::numeric_limits<size_t>::max() >> 1), "Cannot use most significant bit");

	using container_t = std::array<T, MaxSize>;
	container_t internalBuffer{};
	detail::RingBufferHelper helper_{MaxSize};
};


template <typename T, size_t MaxSize>
void
HistoryBuffer<T, MaxSize>::clear()
{
	helper_.clear();
}

template <typename T, size_t MaxSize>
inline bool
HistoryBuffer<T, MaxSize>::empty() const noexcept
{
	return helper_.empty();
}

template <typename T, size_t MaxSize>
inline size_t
HistoryBuffer<T, MaxSize>::size() const noexcept
{
	return helper_.size();
}

template <typename T, size_t MaxSize>
inline void
HistoryBuffer<T, MaxSize>::push_back(const T &element)
{
	auto inner_index = helper_.push_back_location();
	internalBuffer[inner_index] = element;
}

template <typename T, size_t MaxSize>
inline T *
HistoryBuffer<T, MaxSize>::get_at_age(size_t age) noexcept
{
	size_t inner_index = 0;
	if (helper_.age_to_inner_index(age, inner_index)) {
		return &internalBuffer[inner_index];
	}
	return nullptr;
}
template <typename T, size_t MaxSize>
inline const T *
HistoryBuffer<T, MaxSize>::get_at_age(size_t age) const noexcept
{
	size_t inner_index = 0;
	if (helper_.age_to_inner_index(age, inner_index)) {
		return &internalBuffer[inner_index];
	}
	return nullptr;
}

template <typename T, size_t MaxSize>
inline T *
HistoryBuffer<T, MaxSize>::get_at_index(size_t index) noexcept
{
	size_t inner_index = 0;
	if (helper_.index_to_inner_index(index, inner_index)) {
		return &internalBuffer[inner_index];
	}
	return nullptr;
}

template <typename T, size_t MaxSize>
inline const T *
HistoryBuffer<T, MaxSize>::get_at_index(size_t index) const noexcept
{
	size_t inner_index = 0;
	if (helper_.index_to_inner_index(index, inner_index)) {
		return &internalBuffer[inner_index];
	}
	return nullptr;
}

template <typename T, size_t MaxSize>
inline T &
HistoryBuffer<T, MaxSize>::front()
{
	if (empty()) {
		throw std::logic_error("Cannot get the front of an empty buffer");
	}
	return internalBuffer[helper_.front_inner_index()];
}

template <typename T, size_t MaxSize>
inline const T &
HistoryBuffer<T, MaxSize>::front() const
{
	if (empty()) {
		throw std::logic_error("Cannot get the front of an empty buffer");
	}
	return internalBuffer[helper_.front_inner_index()];
}

template <typename T, size_t MaxSize>
inline T &
HistoryBuffer<T, MaxSize>::back()
{
	if (empty()) {
		throw std::logic_error("Cannot get the back of an empty buffer");
	}
	return internalBuffer[helper_.back_inner_index()];
}

template <typename T, size_t MaxSize>
inline const T &
HistoryBuffer<T, MaxSize>::back() const
{
	if (empty()) {
		throw std::logic_error("Cannot get the back of an empty buffer");
	}
	return internalBuffer[helper_.back_inner_index()];
}


} // namespace xrt::auxiliary::util

#include "u_template_historybuf_const_iterator.inl"
#include "u_template_historybuf_iterator.inl"
