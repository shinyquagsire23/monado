// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP type helpers
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#include <stdlib.h>
#include <stdio.h>

#include "ql_xrsp_types.h"
#include "ql_utils.h"

static const char* xrsp_result_lut[33] = {
    "success",
    "invalid argument",
    "invalid data",
    "invalid state",
    "buffer too small",
    "out of memory",
    "topic does not exist",
    "topic already exists",
    "topic is not writable",
    "topic is not readable",
    "no device",
    "invalid transport description",
    "transport closed",
    "I/O error",
    "timeout occurred",
    "packet lost",
    "incompatible packet version",
    "forced termination",
    "property does not exist",
    "no session active",
    "not implemented",
    "unknown error",
    "network host disconnected",
    "ssl memory allocation failed",
    "ssl set cipher list failed",
    "ssl failed to use the provided cert",
    "ssl failed to use the privided private ",
    "cert and key provided failed validation",
    "ssl failed to set read/write fds",
    "ssl handshake failed",
    "peer failed to provide cert",
    "invalid pairing code",
    "pairing refused",
    "pairing timed out",
    "pairing_invalid_cert"
};

static const char* xrsp_topics[33] = {
    "aui4a-adv",
    "hostinfo-adv",
    "Command",
    "Pose",
    "Mesh",
    "Video",
    "Audio",
    "Haptic",
    "Hands",
    "Skeleton",
    "Slice 0",
    "Slice 1",
    "Slice 2",
    "Slice 3",
    "Slice 4",
    "Slice 5",
    "Slice 6",
    "Slice 7",
    "Slice 8",
    "Slice 9",
    "Slice 10",
    "Slice 11",
    "Slice 12",
    "Slice 13",
    "Slice 14",
    "Slice 15",
    "AudioControl",
    "UserSettingsSync",
    "InputControl",
    "Asw",
    "Body",
    "RuntimeIPC",
    "CameraStream",
    "Logging",
};

const char* xrsp_topic_str(int idx) {
    if (idx < 0 || idx > TOPIC_LOGGING) {
        return "unk topic";
    }
    return xrsp_topics[idx];
}

const char* xrsp_builtin_type_str(int idx) {
    switch (idx) {
    case BUILTIN_PAIRING_ACK: return "PAIRING_ACK";
    case BUILTIN_INVITE: return "INVITE";
    case BUILTIN_OK: return "OK";
    case BUILTIN_ACK: return "ACK";
    case BUILTIN_ERROR: return "ERROR";
    case BUILTIN_BYE: return "BYE";
    case BUILTIN_ECHO: return "ECHO";
    case BUILTIN_PAIRING: return "PAIRING";
    case BUILTIN_CODE_GENERATION: return "CODE_GENERATION";
    case BUILTIN_CODE_GENERATION_ACK: return "CODE_GENERATION_ACK";
    case BUILTIN_RESERVED: return "RESERVED";
    default: return "unknown";
    }
}