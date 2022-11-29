// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP type helpers
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#pragma once

#include <stdlib.h>

#include "ql_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Topics
#define TOPIC_AUI4A_ADV (0x0)
#define TOPIC_HOSTINFO_ADV (0x1)
#define TOPIC_COMMAND (0x2)
#define TOPIC_POSE (0x3)
#define TOPIC_MESH (0x4)
#define TOPIC_VIDEO (0x5)
#define TOPIC_AUDIO (0x6)
#define TOPIC_HAPTIC (0x7)
#define TOPIC_HANDS (0x8)
#define TOPIC_SKELETON (0x9)
#define TOPIC_SLICE_0 (0xA)
#define TOPIC_SLICE_1 (0xB)
#define TOPIC_SLICE_2 (0xC)
#define TOPIC_SLICE_3 (0xD)
#define TOPIC_SLICE_4 (0xE)
#define TOPIC_SLICE_5 (0xF)
#define TOPIC_SLICE_6 (0x10)
#define TOPIC_SLICE_7 (0x11)
#define TOPIC_SLICE_8 (0x12)
#define TOPIC_SLICE_9 (0x13)
#define TOPIC_SLICE_10 (0x14)
#define TOPIC_SLICE_11 (0x15)
#define TOPIC_SLICE_12 (0x16)
#define TOPIC_SLICE_13 (0x17)
#define TOPIC_SLICE_14 (0x18)
#define TOPIC_SLICE_15 (0x19)
#define TOPIC_AUDIO_CONTROL (0x1A)
#define TOPIC_USER_SETTINGS_SYNC (0x1B)
#define TOPIC_INPUT_CONTROL (0x1C)
#define TOPIC_ASW (0x1D)
#define TOPIC_BODY (0x1E)
#define TOPIC_RUNTIME_IPC (0x1F)
#define TOPIC_CAMERA_STREAM (0x20)
#define TOPIC_LOGGING (0x21)
#define TOPIC_22 (0x22)
#define TOPIC_23 (0x23)

// TOPIC_HOSTINFO_ADV
#define BUILTIN_PAIRING_ACK (0x0)
#define BUILTIN_INVITE (0x1)
#define BUILTIN_OK (0x2)
#define BUILTIN_ACK (0x3)
#define BUILTIN_ERROR (0x4)
#define BUILTIN_BYE (0x5)
#define BUILTIN_ECHO (0x6)
#define BUILTIN_PAIRING (0x7)
#define BUILTIN_CODE_GENERATION (0x9)
#define BUILTIN_CODE_GENERATION_ACK (0xA)
#define BUILTIN_RESERVED (0xF)

// TOPIC_COMMAND
#define COMMAND_TERMINATE (0x00)
#define COMMAND_1 (0x01)
#define COMMAND_2 (0x02)
#define COMMAND_3 (0x03
#define COMMAND_4 (0x04)
#define COMMAND_5 (0x05)
#define COMMAND_6 (0x06)
#define COMMAND_7 (0x07)
#define COMMAND_8 (0x08)
#define COMMAND_9 (0x09)
#define COMMAND_A (0x0A)
#define COMMAND_RESET_GUARDIAN (0x0B)
#define COMMAND_TOGGLE_CHEMX (0x0C)
#define COMMAND_ENABLE_CAMERA_STREAM (0x0D)
#define COMMAND_DISABLE_CAMERA_STREAM (0x0E)
#define COMMAND_TOGGLE_ASW (0x0F)
#define COMMAND_10 (0x10)
#define COMMAND_DROP_FRAMES_STATE (0x11)

// TOPIC_RUNTIME_IPC
#define RIPC_MSG_CONNECT_TO_REMOTE_SERVER (0x0)
#define RIPC_MSG_RPC (0x1)
#define RIPC_MSG_SERVER_STATE_UPDATE (0x2)
#define RIPC_MSG_ENSURE_SERVICE_STARTED (0x3)

// BUILTIN_ECHO
#define ECHO_PING (0)
#define ECHO_PONG (1)

// Internal
#define STATE_SEGMENT_META (0)
#define STATE_SEGMENT_READ (1)
#define STATE_EXT_READ (2)

#define PAIRINGSTATE_WAIT_FIRST  (0)
#define PAIRINGSTATE_WAIT_SECOND (1)
#define PAIRINGSTATE_PAIRING     (2)
#define PAIRINGSTATE_PAIRED      (3)

#define DEVICE_TYPE_QUEST_1   (1)
#define DEVICE_TYPE_QUEST_2   (2)
#define DEVICE_TYPE_QUEST_PRO (3)

const char* xrsp_topic_str(int idx);
const char* xrsp_builtin_type_str(int idx);

#ifdef __cplusplus
}
#endif