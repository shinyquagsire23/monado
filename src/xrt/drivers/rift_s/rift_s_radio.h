/*
 * Copyright 2020 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 * OpenHMD - Free and Open Source API and drivers for immersive technology.
 *
 * The Rift S talks to remote devices via HID radio control reports
 * the need to be serialised and coordinated to work properly.
 *
 */
/*!
 * @file
 * @brief  Oculus Rift S HMD Radio management interface
 *
 * Functions for serialising requests to communicate with
 * Touch controllers over the HMDs wireless radio link,
 * collecting responses and delivering them back via callbacks.
 *
 * Ported from OpenHMD
 *
 * @author Jan Schmidt <jan@centricular.com>
 */

#pragma once

#ifndef RIFT_S_RADIO_H
#define RIFT_S_RADIO_H

#include "os/os_hid.h"
#include "xrt/xrt_defines.h"

typedef struct rift_s_radio_command rift_s_radio_command;
typedef struct rift_s_radio_state rift_s_radio_state;

typedef void (*rift_s_radio_completion_fn)(bool success,
                                           uint8_t *response_bytes,
                                           int response_bytes_len,
                                           void *cb_data);

struct rift_s_radio_state
{
	bool command_result_pending;
	int last_radio_seqnum;

	rift_s_radio_command *pending_commands;
	rift_s_radio_command *pending_commands_tail;
};

void
rift_s_radio_state_init(rift_s_radio_state *state);
void
rift_s_radio_state_clear(rift_s_radio_state *state);

void
rift_s_radio_update(rift_s_radio_state *state, struct os_hid_device *hid);
void
rift_s_radio_queue_command(rift_s_radio_state *state,
                           const uint64_t device_id,
                           const uint8_t *cmd_bytes,
                           const int cmd_bytes_len,
                           rift_s_radio_completion_fn cb,
                           void *cb_data);
void
rift_s_radio_get_json_block(rift_s_radio_state *state,
                            const uint64_t device_id,
                            rift_s_radio_completion_fn cb,
                            void *cb_data);
#endif
