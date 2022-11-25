// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP segmented topic packets
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#include "ql_xrsp_segmented_pkt.h"

#include <stdlib.h>
#include <stdio.h>

#include "math/m_api.h"
#include "math/m_vec3.h"

#include "ql_xrsp_hostinfo.h"
#include "ql_xrsp_types.h"
#include "ql_types.h"
#include "ql_utils.h"

extern "C"
{

void ql_xrsp_segpkt_init(struct ql_xrsp_segpkt* segpkt, struct ql_xrsp_host* host, int num_segs, ql_xrsp_segpkt_handler_t handler)
{
    segpkt->num_segs = num_segs;
    segpkt->reading_idx = 0;
    segpkt->handler = handler;

    for (int i = 0; i < 3; i++)
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

void ql_xrsp_segpkt_consume(struct ql_xrsp_segpkt* segpkt, struct ql_xrsp_host* host, struct ql_xrsp_topic_pkt* pkt)
{
    if (pkt->payload_valid < 8) {
        return;
    }

    if (pkt->payload_valid == sizeof(uint32_t) * (segpkt->num_segs+1)) {
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
            for (int i = 0; i < segpkt->num_segs; i++)
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
                if (segpkt->reading_idx >= segpkt->num_segs) {
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
    

    /*
    if len(b) < 8:
        print ("Bad pose pkt!")
        hex_dump(b)
        return

    self.host = host
    self.payload = b[0x0:]

    self.printable = ""

    if self.host.pose_state.state == STATE_SEGMENT_META:
        self.host.pose_state.reset()
        self.host.pose_state.type_idx, self.host.pose_state.seg0_size = struct.unpack("<LL", self.payload[:8])

        self.host.pose_state.seg0_size *= 8

        self.host.pose_state.state = STATE_SEGMENT_READ
    elif self.host.pose_state.state == STATE_SEGMENT_READ:

        if len(self.host.pose_state.seg0) < self.host.pose_state.seg0_size:
            self.host.pose_state.seg0 += self.payload
        
        seg0_left = self.host.pose_state.seg0_size - len(self.host.pose_state.seg0)
        
        if seg0_left <= 0:
            try:
                # TODO: store this
                pose_payload = PayloadPose.from_segments([self.host.pose_state.seg0])
                self.printable = pose_payload

                if len(pose_payload.controllers) >= 1:
                    self.host.touchpad_pos = ((pose_payload.controllers[0].touchpadX * (1280/2)) + (1280/2), (pose_payload.controllers[0].touchpadY * (-1024/2)) + (1024/2))
                    self.host.touchpad_z = pose_payload.controllers[0].touchpadPressure
                    #self.host.update()

                #print (self.host.touchpad_pos, self.host.touchpad_z)
            except Exception as e:
                print ("Exception in PosePkt:", e)
                hex_dump(self.host.pose_state.seg0)
            self.host.pose_state.state = STATE_SEGMENT_META
    */
}

}

