// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP topic packets
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#pragma once

#include <stdlib.h>

#include "ql_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xrsp_topic_header
{
    uint16_t version_maybe:3;
    uint16_t has_alignment_padding:1;
    uint16_t packet_version_is_internal:1;
    uint16_t packet_version_number:3;
    uint16_t topic:6;
    uint16_t unk_14_15:2;

    uint16_t num_words;
    uint16_t sequence_num;
    uint16_t pad;
} __attribute__((packed)) xrsp_topic_header;

int32_t ql_xrsp_topic_pkt_create(struct ql_xrsp_topic_pkt* pkt, uint8_t* p_initial, int32_t initial_size);
int32_t ql_xrsp_topic_pkt_append(struct ql_xrsp_topic_pkt* pkt, uint8_t* p_data, int32_t data_size);
void ql_xrsp_topic_pkt_destroy(struct ql_xrsp_topic_pkt* pkt);

void ql_xrsp_topic_pkt_dump(struct ql_xrsp_topic_pkt* pkt);

#ifdef __cplusplus
}
#endif