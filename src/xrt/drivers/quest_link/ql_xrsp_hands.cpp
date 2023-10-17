// Copyright 2022, Collabora, Ltd.
// Copyright 2022-2023 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP hand and body skeleton packets
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#include <stdlib.h>
#include <stdio.h>
#include <iostream>

#include "math/m_api.h"
#include "math/m_vec3.h"

#include "ql_xrsp_hands.h"
#include "ql_xrsp_hostinfo.h"
#include "ql_xrsp_types.h"
#include "ql_types.h"
#include "ql_utils.h"

extern "C"
{

struct hands_header
{
    uint32_t unk_0;
    uint32_t unk_4;
};

typedef struct skeleton_bin
{
    uint32_t unk_00;
    uint32_t unk_04;
    double timestamp;
    uint32_t unk_10;
    uint32_t unk_14;
    uint32_t num_bones;
    uint32_t num_capsules;
    uint32_t unk_20;
    uint32_t unk_24;
    uint32_t unk_28;
    struct ovr_pose_f bones[24];
    int16_t bone_parent_idx[24];
    struct ovr_capsule capsules[20];
    //55C
} __attribute__((packed)) skeleton_bin;

typedef struct hands_bin
{
    uint32_t unk_00;
    uint32_t tracking_status;
    xrt_quat root_orient;
    xrt_vec3 root_pos;
    float unk_2[3];
    xrt_quat bone_rots[24];
    double req_timestamp;
    double sample_timestamp;

    float hand_confidence;
    float hand_scale;

    float finger_confidence[5];

    uint32_t unk_3[2];

    float unk_4[26];
    float unk_5[5];
    float unk_6[7];
    float unk_7[5];
} __attribute__((packed)) hands_bin;

void ovr_convert(ovr_pose_f* inout)
{
    ovr_pose_f tmp = *inout;
    inout->pos.x = tmp.pos.x;
    inout->pos.y = tmp.pos.y;
    inout->pos.z = tmp.pos.z;

    inout->orient.x = tmp.orient.x;
    inout->orient.y = tmp.orient.y;
    inout->orient.z = tmp.orient.z;
    inout->orient.w = tmp.orient.w;
}

void ovr_pose_add(ovr_pose_f* out, const ovr_pose_f* rhs)
{
    ovr_pose_f tmp = *out;

    math_quat_rotate_vec3(&rhs->orient, &out->pos, &tmp.pos);
    math_vec3_accum(&rhs->pos, &tmp.pos);
    math_quat_rotate(&rhs->orient, &out->orient, &out->orient);

    *out = tmp;
}

// TODO: Body poses
void ql_xrsp_handle_body(struct ql_xrsp_host* host, struct ql_xrsp_topic_pkt* pkt)
{
#if 0
    static int body_num = 0;
    printf("Parse body %x\n", pkt->payload_valid);

    char tmp[256];
    snprintf(tmp, 256, "body_bin_%u.bin", body_num);
    FILE* f = fopen(tmp, "wb");
    fwrite(pkt->payload, pkt->payload_valid, 1, f);
    fclose(f);

    body_num++;
#endif
}

void ql_xrsp_handle_skeleton(struct ql_xrsp_host* host, struct ql_xrsp_topic_pkt* pkt)
{
    static int skeleton_num = 0;
    //printf("Parse skeleton %x\n", pkt->payload_valid);

    //char tmp[256];
    //snprintf(tmp, 256, "skeleton_bin_%u.bin", skeleton_num);
    //FILE* f = fopen(tmp, "wb");
    //fwrite(pkt->payload, pkt->payload_valid, 1, f);
    //fclose(f);

    struct ql_hands* ctrl = host->sys->hands;
    struct skeleton_bin* payload = (struct skeleton_bin*)pkt->payload;

    // HACK: We should check the header.
    if (skeleton_num < 2) {

        for (int i = 0; i < 24; i++) {
            int idx = (skeleton_num * 24) + i;
            ctrl->bones_last[idx] = payload->bones[i];
            ctrl->bones_last_raw[idx] = payload->bones[i];
            ctrl->bone_parent_idx[idx] = payload->bone_parent_idx[i];
            if (payload->bone_parent_idx[i] > 0) {
                ctrl->bone_parent_idx[idx] += (skeleton_num * 24);
            }

            int16_t parent = ctrl->bone_parent_idx[idx];
            while (parent > 0) {
                ovr_pose_add(&ctrl->bones_last[idx], &ctrl->bones_last_raw[parent]);
                parent = ctrl->bone_parent_idx[parent];
            }

            ovr_convert(&ctrl->bones_last[idx]);
        }
        //memcpy(ctrl->bones_last, payload->bones, sizeof(ctrl->bones_last));
    }


    skeleton_num++;
}

void dump_hand(struct hands_bin* hand)
{
    printf("header: %x %x\n", hand->unk_00, hand->tracking_status);
    printf("unk_2: %f %f %f\n", hand->unk_2[0], hand->unk_2[1], hand->unk_2[2]);
    printf("unk_2: %f %f %f\n", hand->unk_2[0], hand->unk_2[1], hand->unk_2[2]);
}

void ql_xrsp_handle_hands(struct ql_xrsp_host* host, struct ql_xrsp_topic_pkt* pkt)
{
    os_mutex_lock(&host->pose_mutex);

    struct ql_hands* ctrl = host->sys->hands;

    struct hands_header* header = (struct hands_header*)pkt->payload;
    struct hands_bin* hand_l = (struct hands_bin*)(header+1);
    struct hands_bin* hand_r = (struct hands_bin*)(hand_l+1);

    ctrl->poses[0].orientation = hand_l->root_orient;
    ctrl->poses[0].position = hand_l->root_pos;

    ctrl->poses[1].orientation = hand_r->root_orient;
    ctrl->poses[1].position = hand_r->root_pos;

    // Accumulate the base pose with the rotations.
    for (int skeleton_num = 0; skeleton_num < 2; skeleton_num++)
    {
        for (int i = 0; i < 24; i++) {
            struct hands_bin* hand = skeleton_num ? hand_r : hand_l;
            int shift = (skeleton_num * 24);
            int idx = shift + i;
            ctrl->bones_last[idx] = ctrl->bones_last_raw[idx];

            int16_t parent = ctrl->bone_parent_idx[idx];
            xrt_quat accum = XRT_QUAT_IDENTITY;
            while (parent > 0) {
                ovr_pose_f tmp = ctrl->bones_last_raw[parent];
                tmp.orient = hand->bone_rots[parent - shift];

                math_quat_rotate(&hand->bone_rots[parent - shift], &accum, &accum);
                ovr_pose_add(&ctrl->bones_last[idx], &tmp);

                parent = ctrl->bone_parent_idx[parent];
            }

            math_quat_rotate(&hand->bone_rots[idx - shift], &accum, &accum);
            ctrl->bones_last[idx].orient = accum;

            ovr_convert(&ctrl->bones_last[idx]);
        }
    }
    

    //printf("real l: %f %f %f\n", ctrl->poses[0].position.x, ctrl->poses[0].position.y, ctrl->poses[0].position.z);
    //printf("real r: %f %f %f\n", ctrl->poses[1].position.x, ctrl->poses[1].position.y, ctrl->poses[1].position.z);

    os_mutex_unlock(&host->pose_mutex);
}

}
