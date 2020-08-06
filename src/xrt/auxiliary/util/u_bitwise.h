// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Functions for manipulating tightly packed data as bits.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Pete Black <pete.black@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int
get_bit(const unsigned char *b, int num);

int
get_bits(const unsigned char *b, int start, int num);

/*!
 * Interpret the least-significant 13 bits as a signed 13-bit integer, and cast
 * it to a signed int for normal usage.
 */
int
sign_extend_13(uint32_t i);


#ifdef __cplusplus
}
#endif
