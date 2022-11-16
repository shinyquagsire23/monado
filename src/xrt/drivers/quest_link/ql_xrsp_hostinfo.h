// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP hostinfo packets
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#pragma once

#include <stdlib.h>

#include "ql_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t ql_xrsp_hostinfo_pkt_create(struct ql_xrsp_hostinfo_pkt* pkt, struct ql_xrsp_topic_pkt* topic_pkt, struct ql_xrsp_host* host);
void ql_xrsp_hostinfo_pkt_destroy(struct ql_xrsp_hostinfo_pkt* pkt);

void ql_xrsp_hostinfo_pkt_dump(struct ql_xrsp_hostinfo_pkt* pkt);


uint8_t* ql_xrsp_craft_echo(uint16_t result, uint32_t echo_id, int64_t org, int64_t recv, int64_t xmt, int64_t offset, int32_t* out_len);
uint8_t* ql_xrsp_craft_basic(uint8_t message_type, uint16_t result, uint32_t unk_4, const uint8_t* payload, size_t payload_size, int32_t* out_len);
uint8_t* ql_xrsp_craft_capnp(uint8_t message_type, uint16_t result, uint32_t unk_4, const uint8_t* payload, size_t payload_size, int32_t* out_len);
uint8_t* ql_xrsp_craft(uint8_t message_type, uint16_t result, uint32_t stream_size, uint32_t unk_4, const uint8_t* payload, size_t payload_size, int32_t* out_len);

#ifdef __cplusplus
}
#endif