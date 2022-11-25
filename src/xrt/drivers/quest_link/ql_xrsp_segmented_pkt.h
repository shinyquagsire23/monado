// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP segmented topic packets
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#pragma once

#include <stdlib.h>

#include "ql_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void ql_xrsp_segpkt_init(struct ql_xrsp_segpkt* segpkt, struct ql_xrsp_host* host, int num_segs, ql_xrsp_segpkt_handler_t handler);
void ql_xrsp_segpkt_consume(struct ql_xrsp_segpkt* segpkt, struct ql_xrsp_host* host, struct ql_xrsp_topic_pkt* pkt);


#ifdef __cplusplus
}
#endif