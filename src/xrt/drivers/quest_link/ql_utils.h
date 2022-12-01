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
uint32_t hash_djb2_(const char* s, uint32_t h);

#ifdef __cplusplus
}
#endif

// Constexpr djb2 hash with C++ code
#ifdef __cplusplus
constexpr uint32_t hash_djb2(const char *s, uint32_t h = 5381) {
    return !*s ? h : hash_djb2(s + 1, (33 * h) + (uint8_t)*s);
}

constexpr uint32_t ripc_field_hash(const char *typestr, const char *namestr) {
    return hash_djb2(namestr, hash_djb2(typestr));
}
#else
inline uint32_t hash_djb2(const char* s)
{
    return hash_djb2_(s, 5381);
}

inline uint32_t ripc_field_hash(const char *typestr, const char *namestr)
{
    return hash_djb2_(namestr, hash_djb2_(typestr, 5381));
}
#endif
