// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to quest_link XRSP protocol.
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#pragma once

#include <stdlib.h>

#include "ql_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int ql_xrsp_host_create(struct ql_xrsp_host* host, uint16_t vid, uint16_t pid, int if_num);
void ql_xrsp_host_destroy(struct ql_xrsp_host* host);

int64_t xrsp_ts_ns_from_target(struct ql_xrsp_host *host, int64_t ts);
int64_t xrsp_ts_ns_to_target(struct ql_xrsp_host *host, int64_t ts);
int64_t xrsp_ts_ns(struct ql_xrsp_host *host);

void xrsp_send_to_topic_capnp_wrapped(struct ql_xrsp_host *host, uint8_t topic, uint32_t idx, const uint8_t* data, int32_t data_size);
void xrsp_send_to_topic(struct ql_xrsp_host *host, uint8_t topic, const uint8_t* data, int32_t data_size);

void xrsp_send_simple_haptic(struct ql_xrsp_host *host, int64_t ts, ovr_haptic_target_t controller_id, float amplitude);

#ifdef __cplusplus
}
#endif