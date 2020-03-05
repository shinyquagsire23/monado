// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for manipulating tightly packed data as bits.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Pete Black <pete.black@collabora.com>
 * @ingroup aux_util
 */
#include <stdio.h>
#include "util/u_bitwise.h"

int
get_bit(unsigned char *b, int num)
{
	int index = num / 8;
	return (b[index] >> (7 - (num % 8))) & 1;
}

int
get_bits(unsigned char *b, int start, int num)
{
	int ret = 0;
	for (int i = 0; i < num; i++) {
		ret <<= 1;
		ret |= get_bit(b, start + i);
	}
	return ret;
}

int
sign_extend_13(unsigned int i)
{
	return ((int)(i << 19)) >> 19;
}
