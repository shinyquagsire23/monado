// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP hostinfo packets
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#include <stdlib.h>
#include <stdio.h>
#include <iostream>

#include "math/m_api.h"
#include "math/m_vec3.h"

#include "ql_xrsp.h"
#include "ql_xrsp_segmented_pkt.h"
#include "ql_xrsp_ipc.h"
#include "ql_xrsp_hostinfo.h"
#include "ql_xrsp_types.h"
#include "ql_types.h"
#include "ql_utils.h"

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include "protos/RuntimeIPC.capnp.h"

extern "C"
{

void xrsp_ripc_panel_cmd(struct ql_xrsp_host* host, uint32_t client_id);

void ql_xrsp_ipc_segpkt_init(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host, ql_xrsp_ipc_segpkt_handler_t handler)
{
    segpkt->num_segs = 2;
    segpkt->reading_idx = 0;
    segpkt->handler = handler;

    for (int i = 0; i < 2; i++)
    {
        segpkt->segs[i] = NULL;
        segpkt->segs_valid[i] = 0;
        segpkt->segs_expected[i] = 0;
        segpkt->segs_max[i] = 0;
    }

    for (int i = 0; i < segpkt->num_segs; i++)
    {
        segpkt->segs[i] = (uint8_t*)malloc(0x1000000); // TODO free
        segpkt->segs_valid[i] = 0;
        segpkt->segs_expected[i] = 0;
        segpkt->segs_max[i] = 0x1000000;
    }

    segpkt->state = STATE_SEGMENT_META;
}

void ql_xrsp_ipc_segpkt_consume(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host, struct ql_xrsp_topic_pkt* pkt)
{
    if (pkt->payload_valid < 8) {
        return;
    }

    if (pkt->payload_valid == sizeof(uint32_t) * 2 && *(uint32_t*)pkt->payload == 0) {
        segpkt->state = STATE_SEGMENT_META;
    }

    uint8_t* read_ptr = pkt->payload;
    uint8_t* read_end = read_ptr + pkt->payload_valid;

    while (read_ptr < read_end) {
        if (segpkt->state == STATE_SEGMENT_META) {
            uint32_t* payload = (uint32_t*)pkt->payload;
            segpkt->type_idx = payload[0];
            read_ptr += sizeof(uint32_t);

            // TODO bounds
            for (int i = 0; i < 1; i++)
            {
                segpkt->segs_expected[i] = payload[1+i] * sizeof(uint64_t);
                segpkt->segs_valid[i] = 0;
                read_ptr += sizeof(uint32_t);
            }
            segpkt->reading_idx = 0;
            segpkt->state = STATE_SEGMENT_READ;
        }
        else if (segpkt->state == STATE_SEGMENT_READ) {
            int idx = segpkt->reading_idx;
            uint8_t* write_ptr = segpkt->segs[idx] + segpkt->segs_valid[idx];
            size_t remaining_for_seg = segpkt->segs_expected[idx] - segpkt->segs_valid[idx];
            size_t to_copy = remaining_for_seg > pkt->payload_valid ? pkt->payload_valid : remaining_for_seg;

            memcpy(write_ptr, read_ptr, to_copy);
            segpkt->segs_valid[idx] += to_copy;
            read_ptr += to_copy;
            //printf("Have %x bytes of pose...\n", segpkt->segs_valid[idx]);

            if (segpkt->segs_valid[idx] >= segpkt->segs_expected[idx]) {

                segpkt->reading_idx++;
                if (segpkt->reading_idx == 1) {
                    size_t num_words = segpkt->segs_valid[0] >> 3;

                    kj::ArrayPtr<const capnp::word> dataptr[1] = {kj::arrayPtr((capnp::word*)segpkt->segs[0], num_words)};
                    capnp::SegmentArrayMessageReader message(kj::arrayPtr(dataptr, 1));

                    try
                    {
                        PayloadRuntimeIPC::Reader msg = message.getRoot<PayloadRuntimeIPC>();

                        segpkt->cmd_id = msg.getCmdId();
                        segpkt->next_size = msg.getNextSize();
                        segpkt->client_id = msg.getClientId();
                        segpkt->unk = msg.getUnk();

                        segpkt->segs_expected[segpkt->reading_idx] = segpkt->next_size;
                    }
                    catch(...)
                    {
                        segpkt->cmd_id = 0;
                        segpkt->next_size = 0;
                        segpkt->client_id = 0;
                        segpkt->unk = 0;

                        segpkt->segs_expected[segpkt->reading_idx] = segpkt->next_size;
                    }
                    
                }
                else if (segpkt->reading_idx >= segpkt->num_segs) {
                    //printf("Have %x bytes of pose!\n", segpkt->segs_valid[idx]);
                    //hex_dump(segpkt->segs[idx], segpkt->segs_valid[idx]);
                    //printf("---\n");
                    //hex_dump(pkt->payload, pkt->payload_valid);
                    if (segpkt->handler) {
                        segpkt->handler(segpkt, host);
                    }
                    segpkt->segs_valid[idx] = 0;
                    segpkt->reading_idx = 0;
                    segpkt->state = STATE_SEGMENT_META;
                }
            }
        }
    }
}

void ql_xrsp_handle_ipc(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host)
{
    if (segpkt->client_id == RIPC_FAKE_CLIENT_1 && !host->runtime_connected) {
        xrsp_ripc_connect_to_remote_server(host, host->client_id, "com.oculus.systemdriver", "com.oculus.vrruntimeservice", "RuntimeServiceServer");
        //host->runtime_connected = true;
    }
    else if (segpkt->client_id == RIPC_FAKE_CLIENT_2 && !host->bodyapi_connected) {
        xrsp_ripc_connect_to_remote_server(host, host->client_id+1, "com.oculus.bodyapiservice", "com.oculus.bodyapiservice", "BodyApiServiceServer");
        //host->bodyapi_connected = true;
    }
    else if (segpkt->client_id == RIPC_FAKE_CLIENT_3 && !host->eyetrack_connected) {
        xrsp_ripc_connect_to_remote_server(host, host->client_id+2, "com.oculus.bodyapiservice", "com.oculus.eyetrackingservice", "EyeTrackingServiceServer");
        //host->eyetrack_connected = true;
    }
    else if (segpkt->client_id == RIPC_FAKE_CLIENT_4 && !host->shell_connected)
    {
        xrsp_ripc_connect_to_remote_server(host, host->client_id+3, "com.oculus.os.dialoghost", "com.oculus.os.dialoghost", "DialogHostService");
    }

    //xrsp_ripc_panel_cmd(host, RIPC_FAKE_CLIENT_4);
    //xrsp_ripc_panel_cmd(host, host->client_id+3);

    if (host->shell_connected) {
        //xrsp_ripc_panel_cmd(host, RIPC_FAKE_CLIENT_4);
        //xrsp_ripc_void_bool_cmd(host, host->client_id+3, "EnableEyeTrackingForPCLink"); 
    }

    if (segpkt->client_id == host->client_id) {
        if (!host->runtime_connected) {
            xrsp_ripc_void_bool_cmd(host, host->client_id, "EnableEyeTrackingForPCLink"); 
            xrsp_ripc_void_bool_cmd(host, host->client_id, "EnableFaceTrackingForPCLink");
        }
        host->runtime_connected = true;
        ql_xrsp_handle_runtimeservice_ipc(segpkt, host);
    }
    else if (segpkt->client_id == host->client_id+1) {
        host->bodyapi_connected = true;
        ql_xrsp_handle_bodyapi_ipc(segpkt, host);
    }
    else if (segpkt->client_id == host->client_id+2) {
        host->eyetrack_connected = true;
        ql_xrsp_handle_eyetrack_ipc(segpkt, host);
    }
    else if (segpkt->client_id == host->client_id+3) {
        host->shell_connected = true;
        //ql_xrsp_handle_eyetrack_ipc(segpkt, host);
        printf("Got IPC payload from client %08x, cmd %08x, unk %08x\n", segpkt->client_id, segpkt->cmd_id, segpkt->unk);
        hex_dump(segpkt->segs[1], segpkt->segs_valid[1]);

        //xrsp_ripc_panel_cmd(host, host->client_id+3);
        xrsp_ripc_void_bool_cmd(host, host->client_id+3, "EnableEyeTrackingForPCLink"); 
    }
    else if (segpkt->client_id == RIPC_FAKE_CLIENT_1) {
        ql_xrsp_handle_runtimeservice_events(segpkt, host);
    }
    else if (segpkt->client_id == RIPC_FAKE_CLIENT_2) {
        ql_xrsp_handle_bodyapi_events(segpkt, host);
    }
    else if (segpkt->client_id == RIPC_FAKE_CLIENT_3) {
        ql_xrsp_handle_eyetrack_events(segpkt, host);
    }
    else {
        printf("Got IPC payload from client %08x, cmd %08x, unk %08x\n", segpkt->client_id, segpkt->cmd_id, segpkt->unk);
        hex_dump(segpkt->segs[1], segpkt->segs_valid[1]);
    }
}

void ql_xrsp_ipc_handle_face(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host, uint8_t* read_ptr)
{
    read_ptr += 0xC; // size thing
    read_ptr += 0x2; // version
    read_ptr += 0x4; // unk hash

    read_ptr += 0x4; // size, 0x100
    //printf("%08x\n", *(uint32_t*)(read_ptr));
    if (*(uint32_t*)(read_ptr) != 0x446955AD) return;
    read_ptr += 0x4; // unk hash
    read_ptr += 0x4; // num elements, 0x3F

    // weights_
    float* weights = (float*)read_ptr;
    read_ptr += 0x100-4;

    read_ptr += 0x4; // size, 0x100
    //printf("%08x\n", *(uint32_t*)(read_ptr));
    if (*(uint32_t*)(read_ptr) != 0xBE1EE75B) return;
    read_ptr += 0x4; // unk hash
    read_ptr += 0x4; // num elements, 0x3F

    // weightConfidences_
    float* weights_confidences = (float*)read_ptr; 

    read_ptr += 0x8; // size, unk hash "isValid"
    uint8_t is_valid = *read_ptr;
    read_ptr += 0x8; // size, unk hash "isEyeFollowingBlendshapesValid"
    uint8_t is_eye_following_blendshapes_valid = *read_ptr;

    //printf("Brows: %f %f\n", weights[OVR_EXPRESSION_BROW_LOWERER_L], weights[OVR_EXPRESSION_BROW_LOWERER_R]);
#if 0
    {
        struct ql_controller* ctrl = host->sys->controllers[0];

        ctrl->pose_add.x = 0.0f;
        ctrl->pose_add.y = (1.0 - weights[OVR_EXPRESSION_BROW_LOWERER_L]) * 0.4;
        ctrl->pose_add.z = 0.0f;
    }

    {
        struct ql_controller* ctrl = host->sys->controllers[1];

        ctrl->pose_add.x = 0.0f;
        ctrl->pose_add.y = (1.0 - weights[OVR_EXPRESSION_BROW_LOWERER_R]) * 0.4;
        ctrl->pose_add.z = 0.0f;
    }
#endif
}

typedef struct ovrOneEyeGaze
{
    ovr_pose_f pose; // direction, origin
    float confidence;
    uint32_t is_valid;
} ovrOneEyeGaze;

extern "C"
{
    __attribute__((visibility("default"))) float ql_xrsp_sidechannel_eye_l_orient[4] = {0.0, 0.0, 0.0, 1.0};
    __attribute__((visibility("default"))) float ql_xrsp_sidechannel_eye_r_orient[4] = {0.0, 0.0, 0.0, 1.0};
}

void ql_xrsp_ipc_handle_eyes(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host, uint8_t* read_ptr)
{
    read_ptr += 0x1E;

    ovrOneEyeGaze* eye_l = (ovrOneEyeGaze*) read_ptr;
    ovrOneEyeGaze* eye_r = eye_l + 1;

    //printf("Left:  %f %f %f %f, %f %f %f, %f, %u\n", eye_l->pose.orient.x, eye_l->pose.orient.y, eye_l->pose.orient.z, eye_l->pose.orient.w, eye_l->pose.pos.x, eye_l->pose.pos.y, eye_l->pose.pos.z, eye_l->confidence, eye_l->is_valid);
    //printf("Right: %f %f %f %f, %f %f %f, %f, %u\n", eye_r->pose.orient.x, eye_r->pose.orient.y, eye_r->pose.orient.z, eye_r->pose.orient.w, eye_r->pose.pos.x, eye_r->pose.pos.y, eye_r->pose.pos.z, eye_r->confidence, eye_r->is_valid);

    if (eye_l->confidence > 0.5) {
        ql_xrsp_sidechannel_eye_l_orient[0] = eye_l->pose.orient.x;
        ql_xrsp_sidechannel_eye_l_orient[1] = eye_l->pose.orient.y;
        ql_xrsp_sidechannel_eye_l_orient[2] = eye_l->pose.orient.z;
        ql_xrsp_sidechannel_eye_l_orient[3] = eye_l->pose.orient.w;

        ql_xrsp_sidechannel_eye_r_orient[0] = eye_r->pose.orient.x;
        ql_xrsp_sidechannel_eye_r_orient[1] = eye_r->pose.orient.y;
        ql_xrsp_sidechannel_eye_r_orient[2] = eye_r->pose.orient.z;
        ql_xrsp_sidechannel_eye_r_orient[3] = eye_r->pose.orient.w;
    }
    else {
        ql_xrsp_sidechannel_eye_l_orient[0] = 0.0;
        ql_xrsp_sidechannel_eye_l_orient[1] = 0.0;
        ql_xrsp_sidechannel_eye_l_orient[2] = 0.0;
        ql_xrsp_sidechannel_eye_l_orient[3] = 1.0;

        ql_xrsp_sidechannel_eye_r_orient[0] = 0.0;
        ql_xrsp_sidechannel_eye_r_orient[1] = 0.0;
        ql_xrsp_sidechannel_eye_r_orient[2] = 0.0;
        ql_xrsp_sidechannel_eye_r_orient[3] = 1.0;
    }
    
#if 0
    {
        struct ql_controller* ctrl = host->sys->controllers[0];

        ctrl->pose.orientation.x = eye_l->pose.orient.x;
        ctrl->pose.orientation.y = eye_l->pose.orient.y;
        ctrl->pose.orientation.z = eye_l->pose.orient.z;
        ctrl->pose.orientation.w = eye_l->pose.orient.w;
    }

    {
        struct ql_controller* ctrl = host->sys->controllers[1];

        ctrl->pose.orientation.x = eye_r->pose.orient.x;
        ctrl->pose.orientation.y = eye_r->pose.orient.y;
        ctrl->pose.orientation.z = eye_r->pose.orient.z;
        ctrl->pose.orientation.w = eye_r->pose.orient.w;
    }
#endif
}

void ql_xrsp_ipc_handle_body(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host, uint8_t* read_ptr)
{

}

void ql_xrsp_ipc_handle_state_data(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host, const char* name, uint8_t* read_ptr, uint32_t read_len)
{
    //printf("state: %s\n", name);
    if (!strcmp(name, "expressionWeights_"))
    {
        ql_xrsp_ipc_handle_face(segpkt, host, read_ptr);
    }
    else if (!strcmp(name, "eyeGazes_"))
    {
        ql_xrsp_ipc_handle_eyes(segpkt, host, read_ptr);
    }
    else if (!strcmp(name, "bodyPose_"))
    {
        ql_xrsp_ipc_handle_body(segpkt, host, read_ptr);
    }
    else if (!strcmp(name, "SystemPerformanceState"))
    {
        // TODO
    }
    else if (!strcmp(name, "PerformanceManagerState"))
    {
        // TODO
    }
    else if (!strcmp(name, "KPIFeatureMeasurementsState"))
    {
        // TODO
    }
    else
    {
        printf("Unhandled state: %s\n", name);
        hex_dump(read_ptr, read_len);
    }
}

uint8_t* ql_xrsp_ipc_parse_state(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host, uint8_t* read_ptr)
{
    char name_tmp[64];
    memset(name_tmp, 0, sizeof(name_tmp));

    if (*(uint32_t*)(read_ptr + 4) != ripc_field_hash("std::string", "MemoryName")) return NULL;

    uint32_t to_copy = *(uint32_t*)(read_ptr + 8);
    if (to_copy >= 63) to_copy = 63;

    strncpy(name_tmp, (char*)read_ptr + 0xC, to_copy);
    read_ptr += 0xC;
    read_ptr += to_copy;

    //printf("State: %s\n", name_tmp);

    read_ptr += 0xC; // MemoryId?

    uint32_t to_skip = *(uint32_t*)(read_ptr + 8) + 0x10;
    read_ptr += 0xC;

    ql_xrsp_ipc_handle_state_data(segpkt, host, name_tmp, read_ptr, to_skip);


    return read_ptr + to_skip;
}

void ql_xrsp_ipc_parse_states(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host)
{
    if (*(uint32_t*)(segpkt->segs[1] + 4) != ripc_field_hash("bool", "Success")) return;

    uint32_t num_states = *(uint32_t*)(segpkt->segs[1] + 0x11);
    uint8_t* read_ptr = segpkt->segs[1] + 0x15;

    //printf("There are %u states:\n", num_states);
    for (int i = 0; i < num_states; i++)
    {
        read_ptr = ql_xrsp_ipc_parse_state(segpkt, host, read_ptr);
        if (!read_ptr) break;
    }
}

void ql_xrsp_handle_runtimeservice_ipc(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host)
{
    //printf("From RuntimeServiceServer:\n");
    //hex_dump(segpkt->segs[1], segpkt->segs_valid[1]);
    ql_xrsp_ipc_parse_states(segpkt, host);
}

void ql_xrsp_handle_bodyapi_ipc(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host)
{
    //printf("From BodyApiServiceServer:\n");
    //hex_dump(segpkt->segs[1], segpkt->segs_valid[1]);
    ql_xrsp_ipc_parse_states(segpkt, host);
}

void ql_xrsp_handle_eyetrack_ipc(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host)
{
    //printf("From EyeTrackingServiceServer:\n");
    //hex_dump(segpkt->segs[1], segpkt->segs_valid[1]);
    ql_xrsp_ipc_parse_states(segpkt, host);
}

void ql_xrsp_handle_runtimeservice_events(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host)
{
    //printf("Events From RuntimeServiceServer:\n");
    //hex_dump(segpkt->segs[1], segpkt->segs_valid[1]);
    ql_xrsp_ipc_parse_states(segpkt, host);
}

void ql_xrsp_handle_bodyapi_events(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host)
{
    //printf("Events From BodyApiServiceServer:\n");
    //hex_dump(segpkt->segs[1], segpkt->segs_valid[1]);
    ql_xrsp_ipc_parse_states(segpkt, host);
}

void ql_xrsp_handle_eyetrack_events(struct ql_xrsp_ipc_segpkt* segpkt, struct ql_xrsp_host* host)
{
    //printf("Events From EyeTrackingServiceServer:\n");
    //hex_dump(segpkt->segs[1], segpkt->segs_valid[1]);
    ql_xrsp_ipc_parse_states(segpkt, host);
}

typedef struct ripc_capnp
{
    uint64_t data_info;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint64_t end_info;
} ripc_capnp;

void xrsp_send_ripc_cmd(struct ql_xrsp_host* host, uint32_t cmd_idx, uint32_t client_id, uint32_t unk, const uint8_t* data, int32_t data_size, const uint8_t* extra_data, int32_t extra_data_size)
{

    ::capnp::MallocMessageBuilder message;
    PayloadRuntimeIPC::Builder msg = message.initRoot<PayloadRuntimeIPC>();

    msg.setCmdId(cmd_idx);
    msg.setNextSize(data_size);
    msg.setClientId(client_id);
    msg.setUnk(unk);
    if (extra_data && extra_data_size) {
        msg.setData(kj::arrayPtr(extra_data, extra_data_size));
    }

    kj::ArrayPtr<const kj::ArrayPtr<const capnp::word>> out = message.getSegmentsForOutput();

    uint8_t* packed_data = (uint8_t*)out[0].begin();
    size_t packed_data_size = out[0].size()*sizeof(uint64_t);

    xrsp_send_to_topic_capnp_wrapped(host, TOPIC_RUNTIME_IPC, 0, packed_data, packed_data_size);
    xrsp_send_to_topic(host, TOPIC_RUNTIME_IPC, data, data_size);
}

void xrsp_ripc_ensure_service_started(struct ql_xrsp_host* host, uint32_t client_id, const char* package_name, const char* service_component_name)
{
    uint8_t tmp[0x100];
    uint32_t idx = 0;


    *(uint32_t*)(&tmp[idx]) = strlen(package_name) + sizeof(uint32_t);
    idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = ripc_field_hash("std::string", "PackageName");
    idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = strlen(package_name);
    idx += sizeof(uint32_t);
    strcpy((char*)&tmp[idx], package_name);
    idx += strlen(package_name);

    *(uint32_t*)(&tmp[idx]) = strlen(service_component_name) + sizeof(uint32_t);
    idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = ripc_field_hash("std::string", "ServiceComponentName");
    idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = strlen(service_component_name);
    idx += sizeof(uint32_t);
    strcpy((char*)&tmp[idx], service_component_name);
    idx += strlen(service_component_name);

    *(uint32_t*)(&tmp[idx]) = 0;
    idx += sizeof(uint32_t);

    xrsp_send_ripc_cmd(host, RIPC_MSG_ENSURE_SERVICE_STARTED, client_id, host->session_idx++, tmp, idx, NULL, 0);
}

void xrsp_ripc_connect_to_remote_server(struct ql_xrsp_host* host, uint32_t client_id, const char* package_name, const char* process_name, const char* server_name)
{
    uint8_t tmp[0x100];
    uint32_t idx = 0;


    *(uint32_t*)(&tmp[idx]) = strlen(package_name) + sizeof(uint32_t);
    idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = ripc_field_hash("std::string", "PackageName");
    idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = strlen(package_name);
    idx += sizeof(uint32_t);
    strcpy((char*)&tmp[idx], package_name);
    idx += strlen(package_name);

    *(uint32_t*)(&tmp[idx]) = strlen(process_name) + sizeof(uint32_t);
    idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = ripc_field_hash("std::string", "ProcessName");
    idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = strlen(process_name);
    idx += sizeof(uint32_t);
    strcpy((char*)&tmp[idx], process_name);
    idx += strlen(process_name);

    *(uint32_t*)(&tmp[idx]) = strlen(server_name) + sizeof(uint32_t);
    idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = ripc_field_hash("std::string", "ServerName");
    idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = strlen(server_name);
    idx += sizeof(uint32_t);
    strcpy((char*)&tmp[idx], server_name);
    idx += strlen(server_name);

    *(uint32_t*)(&tmp[idx]) = 0;
    idx += sizeof(uint32_t);

    xrsp_send_ripc_cmd(host, RIPC_MSG_CONNECT_TO_REMOTE_SERVER, client_id, host->session_idx, tmp, idx, NULL, 0);
}

void xrsp_ripc_void_bool_cmd(struct ql_xrsp_host* host, uint32_t client_id, const char* command_name)
{
    uint32_t hash = hash_djb2(command_name);
    hash ^= hash_djb2("Void");
    hash ^= hash_djb2("bool");

    uint8_t tmp[0x100];
    uint32_t idx = 0;

    *(uint16_t*)(&tmp[idx]) = 2;
    idx += sizeof(uint16_t);
    *(uint32_t*)(&tmp[idx]) = hash;
    idx += sizeof(uint32_t);
    *(uint8_t*)(&tmp[idx]) = 0x00;
    idx += sizeof(uint8_t);

    uint8_t tmp2[0x10];
    uint32_t idx2 = 0;
    *(uint32_t*)(&tmp2[idx]) = 1;
    idx2 += sizeof(uint32_t);
    *(uint32_t*)(&tmp2[idx]) = ripc_field_hash("bool", "oneWay");
    idx2 += sizeof(uint32_t);
    *(uint8_t*)(&tmp2[idx]) = 0;
    idx2 += sizeof(uint8_t);
    *(uint32_t*)(&tmp2[idx]) = hash;
    idx2 += sizeof(uint32_t);

    xrsp_send_ripc_cmd(host, RIPC_MSG_RPC, client_id, host->session_idx, tmp, idx, tmp2, idx2); // ripc_extra, sizeof(ripc_extra)
}

void xrsp_ripc_eye_cmd(struct ql_xrsp_host* host, uint32_t client_id, uint32_t cmd)
{
    uint32_t arg_hash = hash_djb2("eyetracking::service::ovrServerCommand");
    uint32_t hash = hash_djb2("ServerCommand");
    hash ^= arg_hash;
    hash ^= hash_djb2("eyetracking::service::ovrServerRPCResult");

    uint8_t tmp[0x100];
    uint32_t idx = 0;

    *(uint16_t*)(&tmp[idx]) = 2;
    idx += sizeof(uint16_t);
    *(uint32_t*)(&tmp[idx]) = hash;
    idx += sizeof(uint32_t);
    //*(uint8_t*)(&tmp[idx]) = 0x00;
    //idx += sizeof(uint8_t);
    *(uint32_t*)(&tmp[idx]) = 0x04;
    idx += sizeof(uint32_t);
    //*(uint32_t*)(&tmp[idx]) = arg_hash ^ 1234;
    //idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = cmd;
    idx += sizeof(uint32_t);
    //*(uint32_t*)(&tmp[idx]) = 0;
    //idx += sizeof(uint32_t);

    uint8_t tmp2[0x10];
    uint32_t idx2 = 0;
    *(uint32_t*)(&tmp2[idx]) = 1;
    idx2 += sizeof(uint32_t);
    *(uint32_t*)(&tmp2[idx]) = ripc_field_hash("bool", "oneWay");
    idx2 += sizeof(uint32_t);
    *(uint8_t*)(&tmp2[idx]) = 0;
    idx2 += sizeof(uint8_t);
    *(uint32_t*)(&tmp2[idx]) = hash;
    idx2 += sizeof(uint32_t);

    xrsp_send_ripc_cmd(host, RIPC_MSG_RPC, client_id, host->session_idx, tmp, idx, tmp2, idx2); // 
}

void xrsp_ripc_panel_cmd(struct ql_xrsp_host* host, uint32_t client_id)
{
    uint32_t arg_hash = hash_djb2("ripc::com::oculus::os::dialoghost::ShowPanelDialogRequest");
    uint32_t hash = hash_djb2("showPanelDialoggg");
    hash ^= arg_hash;
    hash ^= hash_djb2("bool");

    uint8_t tmp[0x100];
    uint32_t idx = 0;

    *(uint16_t*)(&tmp[idx]) = 2;
    idx += sizeof(uint16_t);
    *(uint32_t*)(&tmp[idx]) = hash;
    idx += sizeof(uint32_t);
    //*(uint8_t*)(&tmp[idx]) = 0x00;
    //idx += sizeof(uint8_t);
    *(uint32_t*)(&tmp[idx]) = 0x04;
    idx += sizeof(uint32_t);
    //*(uint32_t*)(&tmp[idx]) = hash_djb2("std::string");
    //idx += sizeof(uint32_t);
    *(uint32_t*)(&tmp[idx]) = 0x0;
    idx += sizeof(uint32_t);

    memset(tmp, 0, sizeof(tmp));
    idx = 4;

    uint8_t tmp2[0x10];
    uint32_t idx2 = 0;
    *(uint32_t*)(&tmp2[idx]) = 1;
    idx2 += sizeof(uint32_t);
    *(uint32_t*)(&tmp2[idx]) = ripc_field_hash("bool", "oneWay");
    idx2 += sizeof(uint32_t);
    *(uint8_t*)(&tmp2[idx]) = 1;
    idx2 += sizeof(uint8_t);
    *(uint32_t*)(&tmp2[idx]) = hash;
    idx2 += sizeof(uint32_t);

    xrsp_send_ripc_cmd(host, RIPC_MSG_RPC, client_id, host->session_idx, tmp, idx, tmp2, idx2); // 
}

}
