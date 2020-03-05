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

#ifdef __cplusplus
extern "C" {
#endif

int
get_bit(unsigned char *b, int num);

int
get_bits(unsigned char *b, int start, int num);

int
sign_extend_13(unsigned int i);


#ifdef __cplusplus
}
#endif
