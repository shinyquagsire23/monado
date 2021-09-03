// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Camera based hand tracking ringbuffer implementation.
 * @author Moses Turner <moses@collabora.com>
 * @ingroup drv_ht
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>


//|  -4   |   -3   |  -2 | -1 | Top | Garbage |
// OR
//|  -4   |   -3   |  -2 | -1 | Top | -7 | -6 | -5 |



template <typename T, int maxSize> struct DiscardLastBuffer
{
	T internalBuffer[maxSize];
	int topIdx = 0;

	/* Put something at the top, overwrite whatever was at the back*/
	void
	push(const T inElement);

	T *operator[](int inIndex);
};

template <typename T, int maxSize>
void
DiscardLastBuffer<T, maxSize>::push(const T inElement)
{
	topIdx++;
	if (topIdx == maxSize) {
		topIdx = 0;
	}

	memcpy(&internalBuffer[topIdx], &inElement, sizeof(T));
}

template <typename T, int maxSize> T *DiscardLastBuffer<T, maxSize>::operator[](int inIndex)
{
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
