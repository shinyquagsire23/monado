// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP topic packets
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#include <stdlib.h>
#include <stdio.h>

#include "ql_xrsp_topic.h"
#include "ql_xrsp_types.h"
#include "ql_types.h"
#include "ql_utils.h"

int32_t ql_xrsp_topic_pkt_create(struct ql_xrsp_topic_pkt* pkt, uint8_t* p_initial, int32_t initial_size, int64_t recv_ns)
{
    *pkt = (struct ql_xrsp_topic_pkt){0};

    if (initial_size < sizeof(xrsp_topic_header)) {
        return -1;
    }

    xrsp_topic_header* header = (xrsp_topic_header*)p_initial;

    pkt->recv_ns = recv_ns;
    pkt->has_alignment_padding = header->has_alignment_padding;
    pkt->packet_version_is_internal = header->packet_version_is_internal;
    pkt->packet_version_number = header->packet_version_number;
    pkt->topic = header->topic;
    pkt->num_words = header->num_words;
    pkt->sequence_num = header->sequence_num;

    if ((pkt->num_words == 0 && pkt->topic == 0 && pkt->sequence_num == 0)
        || (pkt->topic > TOPIC_LOGGING)
        || (pkt->topic == TOPIC_AUI4A_ADV && pkt->num_words == 0xFFFF))
    {
        pkt->payload_size = 0;
        pkt->payload = NULL;
        pkt->remainder_offs = 0;
        pkt->payload_valid = 0;
        //printf("Bad topic pkt?\n");
        //hex_dump(p_initial, 0x10);
        return -1;

    }

    pkt->payload_size = (pkt->num_words - 1) * sizeof(uint32_t);
    pkt->payload = (uint8_t*)malloc(pkt->payload_size);
    pkt->remainder_offs = sizeof(xrsp_topic_header) + pkt->payload_size;

    int32_t initial_size_no_header = initial_size - sizeof(xrsp_topic_header);
    int32_t consumed = initial_size_no_header < pkt->payload_size ? initial_size_no_header : pkt->payload_size;
    memcpy(pkt->payload, p_initial + sizeof(xrsp_topic_header), consumed);
    pkt->payload_valid = consumed;

    // Adjust for alignment padding
    if (pkt->has_alignment_padding && initial_size >= pkt->payload_size) {
        pkt->payload_size -= p_initial[pkt->payload_size-1];
    }

    pkt->missing_bytes = pkt->payload_size - initial_size_no_header;
    if (pkt->missing_bytes < 0) {
        pkt->missing_bytes = 0;
    }

    if (pkt->missing_bytes) 
    {
        //printf("Payload: %x bytes, missing %x, topic %x\n", consumed, pkt->missing_bytes, pkt->topic);
        //hex_dump(p_initial, 0x10);
    }
    else {
        //printf("Payload: %x bytes, missing %x, topic %x\n", consumed, pkt->missing_bytes, pkt->topic);
        //hex_dump(p_initial, 0x10);
    }

    return consumed + sizeof(xrsp_topic_header);
}

int32_t ql_xrsp_topic_pkt_append(struct ql_xrsp_topic_pkt* pkt, uint8_t* p_data, int32_t data_size)
{
    /*
    self.payload += b
        before = len(self.payload)
        self.payload = self.payload[:self.real_size]
        after = len(self.payload)

        if self.bHasAlignPadding and len(self.payload) >= self.real_size:
            self.real_size -= self.payload[self.real_size-1]
            self.payload = self.payload[:self.real_size]

        self.payload_remainder = b[len(b) - (before-after):]
    */

    int32_t consumed = pkt->missing_bytes < data_size ? pkt->missing_bytes : data_size;
    if (consumed) {
        memcpy(pkt->payload + pkt->payload_valid, p_data, consumed);
    }
    pkt->missing_bytes -= consumed;
    pkt->payload_valid += consumed;

    printf("Payload: consumed %x bytes, %x valid, missing %x (topic %x)\n", consumed, pkt->payload_valid, pkt->missing_bytes, pkt->topic);

    return consumed;
}

void ql_xrsp_topic_pkt_destroy(struct ql_xrsp_topic_pkt* pkt)
{
    if (pkt->payload) {
        free(pkt->payload);
    }

    *pkt = (struct ql_xrsp_topic_pkt){0};
}

void ql_xrsp_topic_pkt_dump(struct ql_xrsp_topic_pkt* pkt)
{
    uint8_t muted_topics[] = {TOPIC_AUI4A_ADV, TOPIC_HOSTINFO_ADV, TOPIC_POSE, TOPIC_AUDIO, TOPIC_LOGGING};

    for (int i = 0; i < sizeof(muted_topics); i++)
    {
        if (pkt->topic == muted_topics[i]) return;
    }

    //printf("version_maybe: %x\n", pkt->version_maybe);
    printf("has_alignment_padding: %x\n",  pkt->has_alignment_padding);
    printf("packet_version_is_internal: %x\n",  pkt->packet_version_is_internal);
    printf("packet_version_number: %x\n",  pkt->packet_version_number);
    printf("topic: %s (%x)\n", xrsp_topic_str(pkt->topic), pkt->topic);
    //printf("unk_14_15: %x\n",  pkt->unk_14_15);

    printf("num_words: %x\n",  pkt->num_words);
    printf("sequence_num: %x\n",  pkt->sequence_num);
    //printf("pad: %x\n",  pkt->pad);
    printf("------\n");
}