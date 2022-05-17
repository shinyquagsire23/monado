// Copyright 2021-2022, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief iterator details for ring buffer
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#include <stddef.h>
#include <type_traits>

namespace xrt::auxiliary::util {
template <typename T, size_t MaxSize> class HistoryBuffer;

namespace detail {
	/**
	 * @brief Class template for iterator for HistoryBuffer
	 *
	 * @tparam T Container element type - must match HistoryBuffer
	 * @tparam MaxSize Maximum number of elements - must match HistoryBuffer
	 */
	template <typename T, size_t MaxSize>
	class HistoryBufIterator : public RandomAccessIteratorBase<const RingBufferHelper>
	{
		using base = RandomAccessIteratorBase<const RingBufferHelper>;
		friend class HistoryBuffer<T, MaxSize>;

	public:
		using Self = HistoryBufIterator<T, MaxSize>;
		using container_type = HistoryBuffer<T, MaxSize>;
		using typename base::difference_type;
		using typename base::iterator_category;
		using value_type = T;
		using pointer = T *;
		using reference = T &;

		//! Default-construct an (invalid) iterator.
		HistoryBufIterator() = default;

		// copy and move as you wish
		HistoryBufIterator(HistoryBufIterator const &) = default;
		HistoryBufIterator(HistoryBufIterator &&) noexcept = default;
		HistoryBufIterator &
		operator=(HistoryBufIterator const &) = default;
		HistoryBufIterator &
		operator=(HistoryBufIterator &&) noexcept = default;

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

		//! Get the associated container: for internal use
		container_type *
		container() const noexcept
		{
			return container_;
		}

		//! Dereference operator: throws std::out_of_range if invalid
		reference
		operator*() const;

		//! Smart pointer operator: returns nullptr if invalid
		pointer
		operator->() const noexcept;

		//! Pre-increment: Advance, then return self.
		Self &
		operator++();

		//! Post-increment: return a copy of initial state after incrementing self
		Self
		operator++(int);

		//! Pre-decrement: Subtract, then return self.
		Self &
		operator--();

		//! Post-decrement: return a copy of initial state after decrementing self
		Self
		operator--(int);

		// Use the base class implementation of subtracting one iterator from another
		using base::operator-;

		//! Increment by an arbitrary amount.
		Self &
		operator+=(std::ptrdiff_t n) noexcept;

		//! Decrement by an arbitrary amount.
		Self &
		operator-=(std::ptrdiff_t n) noexcept;

		//! Increment a copy of the iterator by an arbitrary amount.
		Self
		operator+(std::ptrdiff_t n) const noexcept;

		//! Decrement a copy of the iterator by an arbitrary amount.
		Self
		operator-(std::ptrdiff_t n) const noexcept;

	private:
		//! Factory for a "begin" iterator from a container and its helper: mostly for internal use.
		static Self
		begin(container_type &container, const RingBufferHelper &helper)
		{
			return {&container, base::begin(helper)};
		}

		//! Construct the "past the end" iterator that can be decremented safely
		static Self
		end(container_type &container, const RingBufferHelper &helper)
		{
			return {&container, base::end(helper)};
		}

		// for use internally
		HistoryBufIterator(container_type *container, base &&iter_base) : base(iter_base), container_(container)
		{}
		container_type *container_{nullptr};
	};

	template <typename T, size_t MaxSize>
	inline typename HistoryBufIterator<T, MaxSize>::reference
	HistoryBufIterator<T, MaxSize>::operator*() const
	{
		auto *ptr = container_->get_at_index(base::index());
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
	inline HistoryBufIterator<T, MaxSize> &
	HistoryBufIterator<T, MaxSize>::operator++()
	{
		this->increment_n(1);
		return *this;
	}

	template <typename T, size_t MaxSize>
	inline HistoryBufIterator<T, MaxSize>
	HistoryBufIterator<T, MaxSize>::operator++(int)
	{
		HistoryBufIterator tmp = *this;
		this->increment_n(1);
		return tmp;
	}

	template <typename T, size_t MaxSize>
	inline HistoryBufIterator<T, MaxSize> &
	HistoryBufIterator<T, MaxSize>::operator--()
	{
		this->decrement_n(1);
		return *this;
	}

	template <typename T, size_t MaxSize>
	inline HistoryBufIterator<T, MaxSize>
	HistoryBufIterator<T, MaxSize>::operator--(int)
	{
		Self tmp = *this;
		this->decrement_n(1);
		return tmp;
	}

	template <typename T, size_t MaxSize>
	inline HistoryBufIterator<T, MaxSize> &
	HistoryBufIterator<T, MaxSize>::operator+=(std::ptrdiff_t n) noexcept
	{
		static_cast<base &>(*this) += n;
		return *this;
	}

	template <typename T, size_t MaxSize>
	inline HistoryBufIterator<T, MaxSize> &
	HistoryBufIterator<T, MaxSize>::operator-=(std::ptrdiff_t n) noexcept
	{
		static_cast<base &>(*this) -= n;
		return *this;
	}

	template <typename T, size_t MaxSize>
	inline HistoryBufIterator<T, MaxSize>
	HistoryBufIterator<T, MaxSize>::operator+(std::ptrdiff_t n) const noexcept
	{
		Self ret(*this);
		ret += n;
		return ret;
	}

	template <typename T, size_t MaxSize>
	inline HistoryBufIterator<T, MaxSize>
	HistoryBufIterator<T, MaxSize>::operator-(std::ptrdiff_t n) const noexcept
	{
		Self ret(*this);
		ret -= n;
		return ret;
	}

	// Conversion constructor for const iterator
	template <typename T, size_t MaxSize>
	inline HistoryBufConstIterator<T, MaxSize>::HistoryBufConstIterator(const HistoryBufIterator<T, MaxSize> &other)
	    : HistoryBufConstIterator(other.container(), base{other})
	{}
} // namespace detail

// HistoryBuffer method implementations that depend on iterator availability

template <typename T, size_t MaxSize>
inline typename HistoryBuffer<T, MaxSize>::iterator
HistoryBuffer<T, MaxSize>::begin() noexcept
{
	static_assert(std::is_same<typename std::iterator_traits<iterator>::iterator_category,
	                           std::random_access_iterator_tag>::value,
	              "Iterator should be random access");
	return iterator::begin(*this, helper_);
}

template <typename T, size_t MaxSize>
inline typename HistoryBuffer<T, MaxSize>::iterator
HistoryBuffer<T, MaxSize>::end() noexcept
{
	return iterator::end(*this, helper_);
}

} // namespace xrt::auxiliary::util
