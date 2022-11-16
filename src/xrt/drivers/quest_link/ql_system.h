/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 *
 * OpenHMD - Free and Open Source API and drivers for immersive technology.
 */

/*!
 * @file
 * @brief  Meta Quest Link Driver Internal Interface
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_quest_link
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifndef QUEST_LINK_H
#define QUEST_LINK_H

#include "ql_types.h"

extern enum u_logging_level ql_log_level;

#define QUEST_LINK_TRACE(...) U_LOG_IFL_T(ql_log_level, __VA_ARGS__)
#define QUEST_LINK_DEBUG(...) U_LOG_IFL_D(ql_log_level, __VA_ARGS__)
#define QUEST_LINK_INFO(...) U_LOG_IFL_I(ql_log_level, __VA_ARGS__)
#define QUEST_LINK_WARN(...) U_LOG_IFL_W(ql_log_level, __VA_ARGS__)
#define QUEST_LINK_ERROR(...) U_LOG_IFL_E(ql_log_level, __VA_ARGS__)

struct ql_system *
ql_system_create(struct xrt_prober *xp,
						 struct xrt_prober_device *dev,
                         const unsigned char *hmd_serial_no,
                         int if_num);

struct os_hid_device *
ql_system_hid_handle(struct ql_system *sys);

struct ql_tracker *
ql_system_get_tracker(struct ql_system *sys);

struct xrt_device *
ql_system_get_hmd(struct ql_system *sys);
void
ql_system_remove_hmd(struct ql_system *sys);

struct xrt_device *
ql_system_get_controller(struct ql_system *sys, int index);
void
ql_system_remove_controller(struct ql_system *sys, struct ql_controller *ctrl);

struct xrt_device *
ql_system_get_hand_tracking_device(struct ql_system *sys);

void
ql_system_reference(struct ql_system **dst, struct ql_system *src);

#ifdef __cplusplus
}
#endif

#endif
