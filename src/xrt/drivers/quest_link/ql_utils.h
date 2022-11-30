// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Utils for to @ref drv_quest_link.
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#pragma once

#include "ql_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void hex_dump(const uint8_t* b, size_t amt);
uint32_t hash_djb2(const char* s);

#ifdef __cplusplus
}
#endif
