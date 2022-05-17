// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Wrap some ring buffer internals for somewhat generic C usage.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_util
 */

#include "u_id_ringbuffer.h"
#include "u_iterator_base.hpp"
#include "u_template_historybuf_impl_helpers.hpp"
#include "u_logging.h"

#include <vector>
#include <memory>
#include <iterator>
#include <type_traits>


using xrt::auxiliary::util::RandomAccessIteratorBase;
using xrt::auxiliary::util::detail::RingBufferHelper;
using id_value_type = uint64_t;
struct u_id_ringbuffer
{
	u_id_ringbuffer(uint32_t capacity_) : helper(capacity_), ids(capacity_, 0), capacity(capacity_) {}
	RingBufferHelper helper;
	std::vector<id_value_type> ids;
	uint32_t capacity;
};

namespace {

// just-enough iterator to get it done: basically copy and paste from u_template_historybuf_const_iterator.inl
struct IdRingbufferIterator : public RandomAccessIteratorBase<const RingBufferHelper>
{
	using base = RandomAccessIteratorBase<const RingBufferHelper>;
	using Self = IdRingbufferIterator;
	using container_type = const u_id_ringbuffer;
	using typename base::difference_type;
	using typename base::iterator_category;
	using value_type = id_value_type;
	using pointer = const value_type *;
	using reference = const value_type;

	container_type *container_{nullptr};

	IdRingbufferIterator(container_type *container, base &&iter_base) : base(iter_base), container_(container) {}

	static Self
	begin(container_type &container)
	{
		return {&container, base::begin(container.helper)};
	}

	static Self
	end(container_type &container)
	{
		return {&container, base::end(container.helper)};
	}

	//! returns negative if invalid
	int32_t
	inner_index() const
	{
		if (!base::valid()) {
			return -1;
		}
		size_t inner_index = 0;
		if (!container_->helper.index_to_inner_index(base::index(), inner_index)) {
			return -1;
		}
		return static_cast<int32_t>(inner_index);
	}

	//! Dereference operator: throws std::out_of_range if invalid
	uint64_t
	operator*() const
	{
		if (!base::valid()) {
			throw std::out_of_range("Iterator not valid");
		}
		size_t inner_index = 0;
		if (!container_->helper.index_to_inner_index(base::index(), inner_index)) {
			throw std::out_of_range("Iterator not valid");
		}
		return container_->ids[inner_index];
	}


	//! Pre-increment: Advance, then return self.
	Self &
	operator++()
	{
		this->increment_n(1);
		return *this;
	}

	//! Pre-decrement: Subtract, then return self.
	Self &
	operator--()
	{
		this->decrement_n(1);
		return *this;
	}
};

static_assert(std::is_same<typename std::iterator_traits<IdRingbufferIterator>::iterator_category,
                           std::random_access_iterator_tag>::value,
              "Iterator should be random access");
} // namespace

#define DEFAULT_CATCH(...)                                                                                             \
	catch (std::exception const &e)                                                                                \
	{                                                                                                              \
		U_LOG_E("Caught exception: %s", e.what());                                                             \
		return __VA_ARGS__;                                                                                    \
	}                                                                                                              \
	catch (...)                                                                                                    \
	{                                                                                                              \
		U_LOG_E("Caught exception");                                                                           \
		return __VA_ARGS__;                                                                                    \
	}

struct u_id_ringbuffer *
u_id_ringbuffer_create(uint32_t capacity)
{
	try {
		auto ret = std::make_unique<u_id_ringbuffer>(capacity);
		return ret.release();
	}
	DEFAULT_CATCH(nullptr)
}

// Common wrapping to catch exceptions in functions that return an index or negative for error.
template <typename F>
static inline int64_t
exceptionCatchingWrapper(F &&func)
{
	try {
		return func();
	} catch (std::exception const &e) {
		return -1;
	}
}


int64_t
u_id_ringbuffer_push_back(struct u_id_ringbuffer *uirb, uint64_t id)
{
	try {
		auto inner_index = uirb->helper.push_back_location();
		uirb->ids[inner_index] = id;
		return static_cast<int32_t>(inner_index);
	}
	DEFAULT_CATCH(-1)
}

void
u_id_ringbuffer_pop_front(struct u_id_ringbuffer *uirb)
{
	try {
		uirb->helper.pop_front();
	}
	DEFAULT_CATCH()
}

void
u_id_ringbuffer_pop_back(struct u_id_ringbuffer *uirb)
{
	try {
		uirb->helper.pop_back();
	}
	DEFAULT_CATCH()
}


int32_t
u_id_ringbuffer_get_back(struct u_id_ringbuffer *uirb, uint64_t *out_id)
{
	try {
		auto inner_index = uirb->helper.back_inner_index();
		if (inner_index == uirb->capacity) {
			return -1;
		}
		*out_id = uirb->ids[inner_index];
		return static_cast<int32_t>(inner_index);
	}
	DEFAULT_CATCH(-1)
}

int32_t
u_id_ringbuffer_get_front(struct u_id_ringbuffer *uirb, uint64_t *out_id)
{

	try {
		auto inner_index = uirb->helper.front_inner_index();
		if (inner_index == uirb->capacity) {
			return -1;
		}
		*out_id = uirb->ids[inner_index];
		return static_cast<int32_t>(inner_index);
	}
	DEFAULT_CATCH(-1)
}

uint32_t
u_id_ringbuffer_get_size(struct u_id_ringbuffer const *uirb)
{
	try {
		return static_cast<int32_t>(uirb->helper.size());
	}
	DEFAULT_CATCH(0)
}

bool
u_id_ringbuffer_is_empty(struct u_id_ringbuffer const *uirb)
{
	try {
		return uirb->helper.empty();
	}
	DEFAULT_CATCH(true)
}

// Handle conditional setting of value pointed to by out_id, and casting of inner_index
static inline int32_t
handleGetterResult(struct u_id_ringbuffer const *uirb, size_t inner_index, uint64_t *out_id)
{
	if (out_id != nullptr) {
		*out_id = uirb->ids[inner_index];
	}
	return static_cast<int32_t>(inner_index);
}

int32_t
u_id_ringbuffer_get_at_age(struct u_id_ringbuffer *uirb, uint32_t age, uint64_t *out_id)
{
	try {
		size_t inner_index = 0;
		if (!uirb->helper.age_to_inner_index(age, inner_index)) {
			// out of range
			return -1;
		}
		return handleGetterResult(uirb, inner_index, out_id);
	}
	DEFAULT_CATCH(-1)
}

int32_t
u_id_ringbuffer_get_at_clamped_age(struct u_id_ringbuffer *uirb, uint32_t age, uint64_t *out_id)
{
	try {
		size_t inner_index = 0;
		if (!uirb->helper.clamped_age_to_inner_index(age, inner_index)) {
			// out of range
			return -1;
		}
		return handleGetterResult(uirb, inner_index, out_id);
	}
	DEFAULT_CATCH(-1)
}

int32_t
u_id_ringbuffer_get_at_index(struct u_id_ringbuffer *uirb, uint32_t index, uint64_t *out_id)
{
	try {
		size_t inner_index = 0;
		if (!uirb->helper.index_to_inner_index(index, inner_index)) {
			// out of range
			return -1;
		}
		return handleGetterResult(uirb, inner_index, out_id);
	}
	DEFAULT_CATCH(-1)
}

// Handle validity of iterators, conditional setting of values pointed to by out_id and out_index, and return value
static inline int32_t
handleAlgorithmResult(IdRingbufferIterator it, uint64_t *out_id, uint32_t *out_index)
{
	if (!it.valid()) {
		return -1;
	}
	if (out_id != nullptr) {
		*out_id = *it;
	}
	if (out_index != nullptr) {
		*out_index = static_cast<uint32_t>(it.index());
	}
	return it.inner_index();
}

int32_t
u_id_ringbuffer_lower_bound_id(struct u_id_ringbuffer *uirb, uint64_t search_id, uint64_t *out_id, uint32_t *out_index)
{
	try {

		const auto b = IdRingbufferIterator::begin(*uirb);
		const auto e = IdRingbufferIterator::end(*uirb);

		// find the first element *not less than* our ID: binary search
		const auto it = std::lower_bound(b, e, search_id);
		return handleAlgorithmResult(it, out_id, out_index);
	}
	DEFAULT_CATCH(-1)
}

int32_t
u_id_ringbuffer_find_id_unordered(struct u_id_ringbuffer *uirb,
                                  uint64_t search_id,
                                  uint64_t *out_id,
                                  uint32_t *out_index)
{
	try {

		const auto b = IdRingbufferIterator::begin(*uirb);
		const auto e = IdRingbufferIterator::end(*uirb);

		// find the matching ID with simple linear search
		const auto it = std::find(b, e, search_id);
		return handleAlgorithmResult(it, out_id, out_index);
	}
	DEFAULT_CATCH(-1)
}

void
u_id_ringbuffer_destroy(struct u_id_ringbuffer **ptr_to_uirb)
{
	try {
		if (ptr_to_uirb == nullptr) {
			return;
		}
		struct u_id_ringbuffer *uirb = *ptr_to_uirb;
		if (uirb == nullptr) {
			return;
		}
		delete uirb;
		*ptr_to_uirb = nullptr;
	}
	DEFAULT_CATCH()
}
