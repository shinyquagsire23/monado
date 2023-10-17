// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP logging packets
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#include <stdlib.h>
#include <stdio.h>
#include <iostream>

#include "ql_xrsp_logging.h"
#include "ql_xrsp_hostinfo.h"
#include "ql_xrsp_types.h"
#include "ql_types.h"
#include "ql_utils.h"
#include "ql_system.h"

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include "protos/Logging.capnp.h"

extern "C"
{

void ql_xrsp_handle_logging(struct ql_xrsp_host* host, struct ql_xrsp_topic_pkt* pkt)
{
    // TODO parse segment header
    if (pkt->payload_valid <= 8) {
        return;
    }

    try
    {
        size_t num_words = pkt->payload_valid >> 3;

        kj::ArrayPtr<const capnp::word> dataptr[1] = {kj::arrayPtr((capnp::word*)pkt->payload, num_words)};
        capnp::SegmentArrayMessageReader message(kj::arrayPtr(dataptr, 1));

        PayloadLogging::Reader logging = message.getRoot<PayloadLogging>();

        for (LogEntry::Reader entry: logging.getError()) {
            std::string out = entry.getData();
            QUEST_LINK_ERROR("%s", out.c_str());
        }

        for (LogEntry::Reader entry: logging.getWarn()) {
            std::string out = entry.getData();
            QUEST_LINK_WARN("%s", out.c_str());
        }

        for (LogEntry::Reader entry: logging.getDebug()) {
            std::string out = entry.getData();
            QUEST_LINK_DEBUG("%s", out.c_str());
        }

        for (LogEntry::Reader entry: logging.getInfo()) {
            std::string out = entry.getData();
            QUEST_LINK_INFO("%s", out.c_str());
        }
    }
    catch (...)
    {
        QUEST_LINK_ERROR("Failed to parse logging pkt");
    }
    

}

}
