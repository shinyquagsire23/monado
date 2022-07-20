// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Expose std::deque to C
 * @author Mateo de Mayo <mateo.demayo@collabora.com>
 * @ingroup aux_util
 */

#include "u_deque.h"
#include "util/u_time.h"
#include <deque>

using std::deque;

#define U_DEQUE_IMPLEMENTATION(TYPE)                                                                                   \
	u_deque_##TYPE u_deque_##TYPE##_create()                                                                       \
	{                                                                                                              \
		u_deque_##TYPE ud{new deque<TYPE>};                                                                    \
		return ud;                                                                                             \
	}                                                                                                              \
                                                                                                                       \
	void u_deque_##TYPE##_push_back(u_deque_##TYPE ud, TYPE e)                                                     \
	{                                                                                                              \
		deque<TYPE> *d = static_cast<deque<TYPE> *>(ud.ptr);                                                   \
		d->push_back(e);                                                                                       \
	}                                                                                                              \
                                                                                                                       \
	bool u_deque_##TYPE##_pop_front(u_deque_##TYPE ud, TYPE *e)                                                    \
	{                                                                                                              \
		deque<TYPE> *d = static_cast<deque<TYPE> *>(ud.ptr);                                                   \
		bool pop = !d->empty();                                                                                \
		if (pop) {                                                                                             \
			*e = d->front();                                                                               \
			d->erase(d->begin());                                                                          \
		}                                                                                                      \
		return pop;                                                                                            \
	}                                                                                                              \
                                                                                                                       \
	TYPE u_deque_##TYPE##_at(u_deque_##TYPE ud, size_t i)                                                          \
	{                                                                                                              \
		deque<TYPE> *d = static_cast<deque<TYPE> *>(ud.ptr);                                                   \
		return d->at(i);                                                                                       \
	}                                                                                                              \
                                                                                                                       \
	size_t u_deque_##TYPE##_size(u_deque_##TYPE ud)                                                                \
	{                                                                                                              \
		deque<TYPE> *d = static_cast<deque<TYPE> *>(ud.ptr);                                                   \
		return d->size();                                                                                      \
	}                                                                                                              \
                                                                                                                       \
	void u_deque_##TYPE##_destroy(u_deque_##TYPE *ud)                                                              \
	{                                                                                                              \
		deque<TYPE> *d = static_cast<deque<TYPE> *>(ud->ptr);                                                  \
		delete d;                                                                                              \
		ud->ptr = nullptr;                                                                                     \
	}

extern "C" {
U_DEQUE_IMPLEMENTATION(timepoint_ns)
}
