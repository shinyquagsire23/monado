// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for manipulating tightly packed data as bits.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Pete Black <pete.black@collabora.com>
 * @ingroup aux_util
 */
#include "util/u_bitwise.h"

#include <stdio.h>
#include <limits.h>

int
get_bit(const unsigned char *b, int num)
{
	int index = num / CHAR_BIT;
	return (b[index] >> ((CHAR_BIT - 1) - (num % CHAR_BIT))) & 1;
}

int
get_bits(const unsigned char *b, int start, int num)
{
	int ret = 0;
	for (int i = 0; i < num; i++) {
		ret <<= 1;
		ret |= get_bit(b, start + i);
	}
	return ret;
}

int
sign_extend_13(uint32_t i)
{

#define INCOMING_INT_WIDTH (13)
#define ADJUSTMENT ((sizeof(i) * CHAR_BIT) - INCOMING_INT_WIDTH)
	return ((int)(i << ADJUSTMENT)) >> ADJUSTMENT;
}
