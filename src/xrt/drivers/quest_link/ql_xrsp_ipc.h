// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP IPC packets
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#pragma once

#include <stdlib.h>

#include "ql_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void ql_xrsp_ipc_segpkt_init(struct ql_xrsp_ipc_segpkt* segpkt, ql_xrsp_ipc_segpkt_handler_t handler);
void ql_xrsp_ipc_segpkt_destroy(struct ql_xrsp_ipc_segpkt* segpkt);
void ql_xrsp_ipc_segpkt_consume(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host, struct ql_xrsp_topic_pkt* pkt);
void ql_xrsp_handle_ipc(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host);

void ql_xrsp_handle_runtimeservice_events(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host);
void ql_xrsp_handle_bodyapi_events(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host);
void ql_xrsp_handle_eyetrack_events(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host);

void ql_xrsp_handle_runtimeservice_ipc(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host);
void ql_xrsp_handle_bodyapi_ipc(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host);
void ql_xrsp_handle_eyetrack_ipc(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host);

void xrsp_send_ripc_cmd(struct ql_xrsp_host* host, uint32_t cmd_idx, uint32_t client_id, uint32_t unk, const uint8_t* data, int32_t data_size, const uint8_t* extra_data, int32_t extra_data_size);
void xrsp_ripc_ensure_service_started(struct ql_xrsp_host* host, uint32_t client_id, const char* package_name, const char* service_component_name);
void xrsp_ripc_connect_to_remote_server(struct ql_xrsp_host* host, uint32_t client_id, const char* package_name, const char* process_name, const char* server_name);
void xrsp_ripc_void_bool_cmd(struct ql_xrsp_host* host, uint32_t client_id, const char* command_name);
void xrsp_ripc_eye_cmd(struct ql_xrsp_host* host, uint32_t client_id, uint32_t cmd);

#ifdef __cplusplus
}
#endif