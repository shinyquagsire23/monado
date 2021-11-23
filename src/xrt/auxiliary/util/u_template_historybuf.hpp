// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Ringbuffer implementation for keeping track of the past state of things
 * @author Moses Turner <moses@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <assert.h>


//|  -4   |   -3   |  -2 | -1 | Top | Garbage |
// OR
//|  -4   |   -3   |  -2 | -1 | Top | -7 | -6 | -5 |

namespace xrt::auxiliary::util {

template <typename T, size_t MaxSize> class HistoryBuffer;

namespace detail {
	//! All the bookkeeping for adapting a fixed-size array to a ring buffer.
	template <size_t MaxSize> class RingBufferHelper
	{
	private:
		size_t latest_idx_ = 0;
		size_t length_ = 0;

		//! Get the inner index of the front value: assumes not empty!
		size_t
		front_impl_() const noexcept
		{
			assert(!empty());
			// length will not exceed MaxSize, so this will not underflow
			return (latest_idx_ + MaxSize - length_ + 1) % MaxSize;
		}

	public:
		//! Get the inner index for a given age (if possible)
		bool
		age_to_inner_index(size_t age, size_t &out_inner_idx) const noexcept
		{

			if (empty()) {
				return false;
			}
			if (age >= length_) {
				return false;
			}
			// latest_idx_ is the same as latest_idx_ + MaxSize (when modulo MaxSize) so we add it to handle
			// underflow with unsigned values
			out_inner_idx = (latest_idx_ + MaxSize - age) % MaxSize;
			return true;
		}

		//! Get the inner index for a given index (if possible)
		bool
		index_to_inner_index(size_t index, size_t &out_inner_idx) const noexcept
		{

			if (empty()) {
				return false;
			}
			if (index >= length_) {
				return false;
			}
			// Just add to the front index and take modulo MaxSize
			out_inner_idx = (front_impl_() + index) % MaxSize;
			return true;
		}

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
		 */
		size_t
		push_back_location() noexcept
		{
			latest_idx_ = (latest_idx_ + 1) % MaxSize;
			length_ = std::min(length_ + 1, MaxSize);
			return latest_idx_;
		}

		/*!
		 * @brief Record the logical removal of the front element, if any.
		 *
		 * Does nothing if the buffer is empty.
		 */
		void
		pop_front() noexcept
		{
			if (!empty()) {
				length_--;
			}
		}

		//! Get the inner index of the front value, or MaxSize if empty.
		size_t
		front_inner_index() const noexcept
		{
			if (empty()) {
				return MaxSize;
			}
			return front_impl_();
		}

		//! Get the inner index of the back (latest) value, or MaxSize if empty.
		size_t
		back_inner_index() const noexcept
		{
			if (empty()) {
				return MaxSize;
			}
			return latest_idx_;
		}
	};

	/*!
	 * @brief Base class used by iterators and const_iterators of HistoryBuffer, providing substantial parts of the
	 * functionality.
	 *
	 * Using inheritance instead of composition here to more easily satisfy the C++ standard's requirements for
	 * iterators (e.g. that const iterators and iterators are comparable, etc.)
	 *
	 * All invalid instances will compare as equal, but they are not all equivalent: the "past the end" iterator is
	 * invalid to dereference, but is allowed to be decremented.
	 */
	template <size_t MaxSize> class RingBufferIteratorBase
	{
		static_assert(MaxSize < (std::numeric_limits<size_t>::max() >> 1), "Cannot use most significant bit");
		// for internal use
		RingBufferIteratorBase(const RingBufferHelper<MaxSize> *helper, size_t index)
		    : buf_helper_(*helper), index_(index)
		{}

	public:
		//! Construct the "past the end" iterator that can be decremented safely
		static RingBufferIteratorBase
		end(const RingBufferHelper<MaxSize> &helper)
		{
			return RingBufferIteratorBase(helper, helper.size());
		}

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

		/*!
		 * @brief True if this iterator is "irrecoverably" invalid (aka, cleared/default constructed).
		 *
		 * Implies !valid()
		 */
		bool
		is_cleared() const noexcept
		{
			return index_ > MaxSize;
		}

	protected:
		//! default constructor
		RingBufferIteratorBase() = default;

		//! Constructor from a helper and index
		explicit RingBufferIteratorBase(const RingBufferHelper<MaxSize> &helper, size_t index)
		    : buf_helper_(&helper), index_(index % MaxSize)
		{}


		/*!
		 * Clear member variables so we are bytewise identical to a default constructed instance, and thus
		 * irrecoverably invalid.
		 */
		void
		clear() noexcept
		{
			*this = {};
		}

		/*!
		 * @brief Increment the internal index, given appropriate conditions.
		 *
		 * If we were already out of range (invalid), the members are cleared so we are irrecoverably invalid.
		 * However, you can reach the "past-the-end" element using this function and still decrement away from
		 * it.
		 */
		void
		increment() noexcept
		{
			if (valid()) {
				index_++;
			} else {
				// make sure we can't "back out" of invalid
				clear();
			}
		}

		/*!
		 * @brief Decrement the internal index, given appropriate conditions.
		 *
		 * If our index is not 0 and not our sentinel (MaxSize), we decrement.
		 * (This is a broader condition than "valid()": we want to be able to decrement() on the "past-the-end"
		 * iterator to get the last element, etc.)  If we were already out of range (invalid) the members are
		 * cleared so we are irrecoverably invalid.
		 */
		void
		decrement() noexcept
		{
			if (index_ > 0 && !is_cleared()) {
				index_--;
			} else {
				// make sure we can't "back out" of invalid
				clear();
			}
		}

	public:
		/*!
		 * @brief Compute the difference between two iterators.
		 *
		 * - If both are invalid, the result is 0.
		 * - If both are either valid or the "past-the-end" iterator, the result is the difference in (logical,
		 * not inner) index.
		 *
		 * @throws std::logic_error if one of the above conditions is not met.
		 */
		std::ptrdiff_t
		operator-(const RingBufferIteratorBase<MaxSize> &other) const;

		/*!
		 * @brief Increment by an arbitrary value.
		 */
		RingBufferIteratorBase &
		operator+=(std::ptrdiff_t n)
		{
			if (is_cleared()) {
				return *this;
			}

			size_t n_clean = remap_to_range(n);
			index_ = (index_ + n_clean) % MaxSize;
			return *this;
		}

		/*!
		 * @brief Decrement by an arbitrary value.
		 */
		RingBufferIteratorBase &
		operator-=(std::ptrdiff_t n)
		{
			if (is_cleared()) {
				return *this;
			}
			size_t n_clean = remap_to_range(n);
			index_ = (index_ + MaxSize - n_clean) % MaxSize;
			return *this;
		}
		/*!
		 * @brief Add a count.
		 */
		RingBufferIteratorBase
		operator+(std::ptrdiff_t n) const
		{
			RingBufferIteratorBase ret = *this;
			ret += n;
			return ret;
		}

		/*!
		 * @brief Subtract a count.
		 */
		RingBufferIteratorBase
		operator-(std::ptrdiff_t n) const
		{
			RingBufferIteratorBase ret = *this;
			ret -= n;
			return ret;
		}

	private:
		//! Returns a value in [0,  MaxSize)
		static size_t
		remap_to_range(std::ptrdiff_t n)
		{
			if (n < 0) {
				size_t ret = MaxSize - (static_cast<size_t>(-1 * n) % MaxSize);
				assert(ret < MaxSize);
				return ret;
			}
			return static_cast<size_t>(n % MaxSize);
		}
		const RingBufferHelper<MaxSize> *buf_helper_{nullptr};
		size_t index_{MaxSize + 1};
	};

	/*!
	 * Equality comparison operator for @ref RingBufferIteratorBase, which is the base of all HistoryBuffer iterator
	 * types.
	 */
	template <size_t MaxSize>
	static inline bool
	operator==(RingBufferIteratorBase<MaxSize> const &lhs, RingBufferIteratorBase<MaxSize> const &rhs) noexcept
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
	 * Inequality comparison operator for @ref RingBufferIteratorBase, which is the base of all HistoryBuffer
	 * iterator types.
	 */
	template <size_t MaxSize>
	static inline bool
	operator!=(RingBufferIteratorBase<MaxSize> const &lhs, RingBufferIteratorBase<MaxSize> const &rhs) noexcept
	{
		return !(lhs == rhs);
	}

	template <typename T, size_t MaxSize> class HistoryBufIterator;

	/**
	 * @brief Class template for const_iterator for HistoryBuffer
	 *
	 * @tparam T Container element type - must match HistoryBuffer
	 * @tparam MaxSize Maximum number of elements - must match HistoryBuffer
	 */
	template <typename T, size_t MaxSize> class HistoryBufConstIterator : public RingBufferIteratorBase<MaxSize>
	{
		using base = RingBufferIteratorBase<MaxSize>;
		// for use internally
		HistoryBufConstIterator(const HistoryBuffer<T, MaxSize> *container,
		                        RingBufferIteratorBase<MaxSize> &&iter_base)
		    : base(std::move(iter_base)), container_(container)
		{}

	public:
		using iterator_category = std::random_access_iterator_tag;
		using value_type = const T;
		using difference_type = std::ptrdiff_t;
		using pointer = const T *;
		using reference = const T &;
		//! Default-construct an (invalid) iterator.
		HistoryBufConstIterator() = default;

		//! Construct from a container, its helper, and an index: mostly for internal use.
		HistoryBufConstIterator(const HistoryBuffer<T, MaxSize> &container,
		                        const RingBufferHelper<MaxSize> &helper,
		                        size_t index);

		//! Implicit conversion from a non-const iterator
		HistoryBufConstIterator(const HistoryBufIterator<T, MaxSize> &nonconst);

		//! Construct the "past the end" iterator that can be decremented safely
		static HistoryBufConstIterator
		end(const HistoryBuffer<T, MaxSize> &container, const RingBufferHelper<MaxSize> &helper)
		{
			return {&container, RingBufferIteratorBase<MaxSize>::end(helper)};
		}

		//! Is this iterator valid?
		bool
		valid() const noexcept
		{
			return container_ != nullptr && base::valid();
		}

		//! Is this iterator valid?
		explicit operator bool() const noexcept
		{
			return valid();
		}

		//! Dereference operator: throws std::out_of_range if invalid
		reference
		operator*() const;

		//! Smart pointer operator: returns nullptr if invalid
		pointer
		operator->() const noexcept;

		//! Pre-increment: Advance, then return self.
		HistoryBufConstIterator &
		operator++()
		{
			base::increment();
			return *this;
		}

		//! Post-increment: return a copy of initial state after incrementing self
		HistoryBufConstIterator
		operator++(int)
		{
			HistoryBufConstIterator tmp = *this;
			base::increment();
			return tmp;
		}

		//! Pre-decrement: Subtract, then return self.
		HistoryBufConstIterator &
		operator--()
		{
			base::decrement();
			return *this;
		}

		//! Post-decrement: return a copy of initial state after decrementing self
		HistoryBufConstIterator
		operator--(int)
		{
			HistoryBufConstIterator tmp = *this;
			base::decrement();
			return tmp;
		}


		using base::operator-;

		HistoryBufConstIterator &
		operator+=(std::ptrdiff_t n) noexcept
		{
			static_cast<base &>(*this) += n;

			return *this;
		}

		HistoryBufConstIterator &
		operator-=(std::ptrdiff_t n) noexcept
		{
			static_cast<base &>(*this) -= n;

			return *this;
		}

		//! Increment a copy of the iterator a given number of steps.
		HistoryBufConstIterator
		operator+(std::ptrdiff_t n) const noexcept
		{
			HistoryBufConstIterator ret(*this);
			ret += n;
			return ret;
		}

		//! Decrement a copy of the iterator a given number of steps.
		HistoryBufConstIterator
		operator-(std::ptrdiff_t n) const noexcept
		{
			HistoryBufConstIterator ret(*this);
			ret -= n;
			return ret;
		}

	private:
		const HistoryBuffer<T, MaxSize> *container_{nullptr};
	};


	/**
	 * @brief Class template for iterator for HistoryBuffer
	 *
	 * @see HistoryBufConstIterator for the const_iterator version
	 *
	 * @tparam T Container element type - must match HistoryBuffer
	 * @tparam MaxSize Maximum number of elements - must match HistoryBuffer
	 */
	template <typename T, size_t MaxSize> class HistoryBufIterator : public RingBufferIteratorBase<MaxSize>
	{
		using base = RingBufferIteratorBase<MaxSize>;

		// for use internally
		HistoryBufIterator(HistoryBuffer<T, MaxSize> *container, RingBufferIteratorBase<MaxSize> &&iter_base)
		    : base(std::move(iter_base)), container_(container)
		{}

	public:
		using iterator_category = std::random_access_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = T *;
		using reference = T &;

		//! Default-construct an (invalid) iterator.
		HistoryBufIterator() = default;

		//! Construct from a container, its helper, and an index: mostly for internal use.
		HistoryBufIterator(HistoryBuffer<T, MaxSize> &container,
		                   const RingBufferHelper<MaxSize> &helper,
		                   size_t index);

		//! Construct the "past the end" iterator that can be decremented safely
		static HistoryBufIterator
		end(HistoryBuffer<T, MaxSize> &container, const RingBufferHelper<MaxSize> &helper)
		{
			return {&container, RingBufferIteratorBase<MaxSize>::end(helper)};
		}

		//! Is this iterator valid?
		bool
		valid() const noexcept
		{
			return container_ != nullptr && base::valid();
		}

		//! Is this iterator valid?
		explicit operator bool() const noexcept
		{
			return valid();
		}

		//! Dereference operator: throws std::out_of_range if invalid
		reference
		operator*() const;

		//! Smart pointer operator: returns nullptr if invalid
		pointer
		operator->() const noexcept;

		//! Pre-increment: Advance, then return self.
		HistoryBufIterator &
		operator++()
		{
			base::increment();
			return *this;
		}

		//! Post-increment: return a copy of initial state after incrementing self
		HistoryBufIterator
		operator++(int)
		{
			HistoryBufIterator tmp = *this;
			base::increment();
			return tmp;
		}

		//! Pre-decrement: Subtract, then return self.
		HistoryBufIterator &
		operator--()
		{
			base::decrement();
			return *this;
		}

		//! Post-decrement: return a copy of initial state after decrementing self
		HistoryBufIterator
		operator--(int)
		{
			HistoryBufIterator tmp = *this;
			base::decrement();
			return tmp;
		}
		using base::operator-;

		HistoryBufIterator &
		operator+=(std::ptrdiff_t n) noexcept
		{
			static_cast<base &>(*this) += n;

			return *this;
		}

		HistoryBufIterator &
		operator-=(std::ptrdiff_t n) noexcept
		{
			static_cast<base &>(*this) -= n;

			return *this;
		}

		//! Increment a copy of the iterator a given number of steps.
		HistoryBufIterator
		operator+(std::ptrdiff_t n) const noexcept
		{
			HistoryBufIterator ret(*this);
			ret += n;
			return ret;
		}

		//! Decrement a copy of the iterator a given number of steps.
		HistoryBufIterator
		operator-(std::ptrdiff_t n) const noexcept
		{
			HistoryBufIterator ret(*this);
			ret -= n;
			return ret;
		}

	private:
		HistoryBuffer<T, MaxSize> *container_{nullptr};
	};


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
private:
	using container_t = std::array<T, MaxSize>;
	container_t internalBuffer{};
	detail::RingBufferHelper<MaxSize> helper_;

	static_assert(MaxSize < (std::numeric_limits<size_t>::max() >> 1), "Cannot use most significant bit");

public:
	//! Is the buffer empty?
	bool
	empty() const noexcept
	{
		return helper_.empty();
	}
	//! How many elements are in the buffer?
	size_t
	size() const noexcept
	{
		return helper_.size();
	}


	/*!
	 * @brief Put something at the back, overwriting whatever was at the front if necessary.
	 *
	 * This is permitted to invalidate iterators. They won't be poisoned, but they will return something you don't
	 * expect.
	 */
	void
	push_back(const T &element)
	{
		auto inner_index = helper_.push_back_location();
		internalBuffer[inner_index] = element;
	}

	//! @overload
	void
	push_back(T &&element)
	{
		auto inner_index = helper_.push_back_location();
		internalBuffer[inner_index] = std::move(element);
	}

	/*!
	 * @brief Logically remove the oldest element from the buffer.
	 *
	 * The value still remains in the backing container until overwritten, it just isn't accessible anymore.
	 *
	 * This invalidates iterators. They won't be poisoned, but they will return something you don't expect.
	 */
	void
	pop_front()
	{
		helper_.pop_front();
	}

	/*!
	 * @brief Access something at a given age, where age 0 is the most recent value, age 1 precedes it, etc.
	 *
	 * Out of bounds accesses will return nullptr.
	 */
	T *
	get_at_age(size_t age) noexcept
	{
		size_t inner_index = 0;
		if (helper_.age_to_inner_index(age, inner_index)) {
			return &internalBuffer[inner_index];
		}
		return nullptr;
	}


	//! @overload
	const T *
	get_at_age(size_t age) const noexcept
	{
		size_t inner_index = 0;
		if (helper_.age_to_inner_index(age, inner_index)) {
			return &internalBuffer[inner_index];
		}
		return nullptr;
	}

	/*!
	 * @brief Access something at a given index, where 0 is the least-recent value still stored, index 1 follows it,
	 * etc.
	 *
	 * Out of bounds accesses will return nullptr.
	 */
	T *
	get_at_index(size_t index) noexcept
	{
		size_t inner_index = 0;
		if (helper_.index_to_inner_index(index, inner_index)) {
			return &internalBuffer[inner_index];
		}
		return nullptr;
	}

	//! @overload
	const T *
	get_at_index(size_t index) const noexcept
	{
		size_t inner_index = 0;
		if (helper_.index_to_inner_index(index, inner_index)) {
			return &internalBuffer[inner_index];
		}
		return nullptr;
	}



	//! Access the ring buffer helper, mostly for implementation usage only.
	const detail::RingBufferHelper<MaxSize> &
	helper() const noexcept
	{
		return helper_;
	}

	using iterator = detail::HistoryBufIterator<T, MaxSize>;
	using const_iterator = detail::HistoryBufConstIterator<T, MaxSize>;

	//! Get an iterator for the least-recent element.
	iterator
	begin() noexcept
	{
		return iterator(*this, helper_, 0);
	}

	//! Get a "past the end" iterator
	iterator
	end() noexcept
	{
		return iterator::end(*this, helper_);
	}

	//! Get a const iterator for the least-recent element.
	const_iterator
	cbegin() const noexcept
	{
		return const_iterator(*this, helper_, 0);
	}

	//! Get a "past the end" const iterator
	const_iterator
	cend() const noexcept
	{
		return const_iterator::end(*this, helper_);
	}

	//! Get a const iterator for the least-recent element.
	const_iterator
	begin() const noexcept
	{
		return cbegin();
	}

	//! Get a "past the end" const iterator
	const_iterator
	end() const noexcept
	{
		return cend();
	}

	/*!
	 * @brief Gets a reference to the front (least-recent) element in the buffer.
	 * @throws std::logic_error if buffer is empty
	 */
	T &
	front()
	{
		if (empty()) {
			throw std::logic_error("Cannot get the front of an empty buffer");
		}
		return internalBuffer[helper_.front_inner_index()];
	}
	//! @overload
	const T &
	front() const
	{
		if (empty()) {
			throw std::logic_error("Cannot get the front of an empty buffer");
		}
		return internalBuffer[helper_.front_inner_index()];
	}

	/*!
	 * @brief Gets a reference to the back (most-recent) element in the buffer.
	 * @throws std::logic_error if buffer is empty
	 */
	T &
	back()
	{
		if (empty()) {
			throw std::logic_error("Cannot get the back of an empty buffer");
		}
		return internalBuffer[helper_.back_inner_index()];
	}

	//! @overload
	const T &
	back() const
	{
		if (empty()) {
			throw std::logic_error("Cannot get the back of an empty buffer");
		}
		return internalBuffer[helper_.back_inner_index()];
	}
};

namespace detail {

	template <size_t MaxSize>
	inline bool
	RingBufferIteratorBase<MaxSize>::valid() const noexcept
	{
		return index_ < MaxSize && buf_helper_ != nullptr && index_ < buf_helper_->size();
	}

	template <size_t MaxSize>
	inline std::ptrdiff_t
	detail::RingBufferIteratorBase<MaxSize>::operator-(const RingBufferIteratorBase<MaxSize> &other) const
	{
		const bool self_valid = valid();
		const bool other_valid = other.valid();
		if (!self_valid && !other_valid) {
			return 0;
		}
		if (index_ < MaxSize && other.index_ < MaxSize) {
			return static_cast<std::ptrdiff_t>(index_) - static_cast<std::ptrdiff_t>(other.index_);
		}
		throw std::logic_error(
		    "Tried to find the difference between an iterator that has no concrete index, and one that does.");
	}

	template <typename T, size_t MaxSize>
	inline HistoryBufIterator<T, MaxSize>::HistoryBufIterator(HistoryBuffer<T, MaxSize> &container,
	                                                          const RingBufferHelper<MaxSize> &helper,
	                                                          size_t index)
	    : base(helper, index), container_(&container)
	{}

	template <typename T, size_t MaxSize>
	inline typename HistoryBufIterator<T, MaxSize>::reference
	HistoryBufIterator<T, MaxSize>::operator*() const
	{
		auto ptr = container_->get_at_index(base::index());
		if (ptr == nullptr) {
			throw std::out_of_range("Iterator index out of range");
		}
		return *ptr;
	}

	template <typename T, size_t MaxSize>
	inline typename HistoryBufIterator<T, MaxSize>::pointer
	HistoryBufIterator<T, MaxSize>::operator->() const noexcept
	{
		return container_->get_at_index(base::index());
	}

	template <typename T, size_t MaxSize>
	inline HistoryBufConstIterator<T, MaxSize>::HistoryBufConstIterator(const HistoryBuffer<T, MaxSize> &container,
	                                                                    const RingBufferHelper<MaxSize> &helper,
	                                                                    size_t index)
	    : base(helper, index), container_(&container)
	{}

	template <typename T, size_t MaxSize>
	inline detail::HistoryBufConstIterator<T, MaxSize>::HistoryBufConstIterator(
	    const HistoryBufIterator<T, MaxSize> &nonconst)
	    : base(nonconst), container_(nonconst.container_)
	{}

	template <typename T, size_t MaxSize>
	inline typename HistoryBufConstIterator<T, MaxSize>::reference
	HistoryBufConstIterator<T, MaxSize>::operator*() const
	{
		auto ptr = container_->get_at_index(base::index());
		if (ptr == nullptr) {
			throw std::out_of_range("Iterator index out of range");
		}
		return *ptr;
	}

	template <typename T, size_t MaxSize>
	inline typename HistoryBufConstIterator<T, MaxSize>::pointer
	HistoryBufConstIterator<T, MaxSize>::operator->() const noexcept
	{
		return container_->get_at_index(base::index());
	}
} // namespace detail
} // namespace xrt::auxiliary::util
