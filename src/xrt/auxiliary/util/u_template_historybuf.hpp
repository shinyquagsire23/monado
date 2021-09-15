// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Ringbuffer implementation for keeping track of the past state of things
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

#include <algorithm>
#include <assert.h>


//|  -4   |   -3   |  -2 | -1 | Top | Garbage |
// OR
//|  -4   |   -3   |  -2 | -1 | Top | -7 | -6 | -5 |

namespace xrt::auxiliary::util {

template <typename T, int maxSize> struct HistoryBuffer
{
	T internalBuffer[maxSize];
	int topIdx = 0;
	int length = 0;

	/* Put something at the top, overwrite whatever was at the back*/
	void
	push(const T inElement);

	T *operator[](int inIndex);

	// Lazy convenience.
	T *
	last();
};

template <typename T, int maxSize>
void
HistoryBuffer<T, maxSize>::push(const T inElement)
{
	topIdx++;
	if (topIdx == maxSize) {
		topIdx = 0;
	}

	memcpy(&internalBuffer[topIdx], &inElement, sizeof(T));
	length++;
	length = std::min(length, maxSize);
	// U_LOG_E("new length is %zu", length);
}

template <typename T, int maxSize> T *HistoryBuffer<T, maxSize>::operator[](int inIndex)
{
	if (length == 0) {
		return NULL;
	}
	assert(inIndex <= maxSize);
	assert(inIndex >= 0);

	int index = topIdx - inIndex;
	if (index < 0) {
		index = maxSize + index;
	}

	assert(index >= 0);
	if (index > maxSize) {
		assert(false);
	}

	return &internalBuffer[index];
}
} // namespace xrt::auxiliary::util