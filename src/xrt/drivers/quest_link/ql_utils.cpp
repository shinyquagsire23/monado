// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Utils for to @ref drv_quest_link.
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#include "ql_utils.h"

#include <stdio.h>

void hex_dump(const uint8_t* b, size_t amt)
{
    for (size_t i = 0; i < amt; i++)
    {
        if (i && i % 16 == 0) {
            printf("\n");
        }
        printf("%02x ", b[i]);
    }
    printf("\n");
}

uint32_t hash_djb2(const char* s)
{
    uint32_t hash = 5381;
    uint32_t len = strlen(s);
    for (int i = 0; i < len; i++)
    {
        hash = ((hash << 5) + hash) + s[i];
    }
    return hash;
}

