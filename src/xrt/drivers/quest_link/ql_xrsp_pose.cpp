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

#include "ql_xrsp_pose.h"
#include "ql_xrsp_hostinfo.h"
#include "ql_xrsp_types.h"
#include "ql_types.h"
#include "ql_utils.h"

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include "protos/Pose.capnp.h"

extern "C"
{

void ql_xrsp_handle_pose(struct ql_xrsp_host* host, struct ql_xrsp_topic_pkt* pkt)
{
    printf("Parse pose\n");

    // TODO parse segment header
    if (pkt->payload_valid <= 8) {
        return;
    }

    size_t num_words = pkt->payload_valid >> 3;

    kj::ArrayPtr<const capnp::word> dataptr[1] = {kj::arrayPtr((capnp::word*)pkt->payload, num_words)};
    capnp::SegmentArrayMessageReader message(kj::arrayPtr(dataptr, 1));

    PayloadPose::Reader pose = message.getRoot<PayloadPose>();

    //std::cout << pose << "\n";
    int idx = 0;
    for (PoseTrackedController::Reader controller: pose.getControllers()) {
        struct ql_controller* ctrl = host->sys->controllers[idx++];
        printf("%x\n", controller.getButtons());
        OvrPoseF::Reader controllerPose = controller.getPose();

        ctrl->pose.position.x = controllerPose.getPosX();
        ctrl->pose.position.y = controllerPose.getPosY();
        ctrl->pose.position.z = controllerPose.getPosZ();

        ctrl->pose.orientation.x = controllerPose.getQuatX();
        ctrl->pose.orientation.y = controllerPose.getQuatY();
        ctrl->pose.orientation.z = controllerPose.getQuatZ();
        ctrl->pose.orientation.w = controllerPose.getQuatW();
    }

    OvrPoseF::Reader headsetPose = pose.getHeadset();
    struct ql_hmd* hmd = host->sys->hmd;

    hmd->pose.position.x = headsetPose.getPosX();
    hmd->pose.position.y = headsetPose.getPosY();
    hmd->pose.position.z = headsetPose.getPosZ();

    hmd->pose.orientation.x = headsetPose.getQuatX();
    hmd->pose.orientation.y = headsetPose.getQuatY();
    hmd->pose.orientation.z = headsetPose.getQuatZ();
    hmd->pose.orientation.w = headsetPose.getQuatW();
}

}
