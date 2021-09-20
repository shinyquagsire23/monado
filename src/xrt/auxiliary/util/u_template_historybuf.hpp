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

template <typename T, int maxSize> class HistoryBuffer
{
private:
	T internalBuffer[maxSize];
	int mTopIdx = 0;
	int mLength = 0;


public:
	// clang-format off
	int topIdx() { return mTopIdx; }
	int length() { return mLength; }
	// clang-format on

	/* Put something at the top, overwrite whatever was at the back*/
	void
	push(const T inElement)
	{
		mTopIdx++;
		if (mTopIdx == maxSize) {
			mTopIdx = 0;
		}

		memcpy(&internalBuffer[mTopIdx], &inElement, sizeof(T));
		mLength++;
		mLength = std::min(mLength, maxSize);
	}

	T * // Hack comment to fix clang-format
	operator[](int inIndex)
	{
		if (mLength == 0) {
			return NULL;
		}
		assert(inIndex <= maxSize);
		assert(inIndex >= 0);

		int index = mTopIdx - inIndex;
		if (index < 0) {
			index = maxSize + index;
		}

		assert(index >= 0);
		if (index > maxSize) {
			assert(false);
		}

		return &internalBuffer[index];
	}
};

} // namespace xrt::auxiliary::util
