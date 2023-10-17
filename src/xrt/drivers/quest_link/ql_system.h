/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * Copyright 2022-2023 Max Thomas
 * SPDX-License-Identifier: BSL-1.0
 *
 */
/*!
 * @file
 * @brief  Meta Quest Link headset tracking system
 *
 * The Quest Link system instantiates the HMD, controller,
 * and hand devices, and manages refcounts
 *
 * @author Max Thomas <mtinc2@gmail.com>
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

struct xrt_device *
ql_system_get_hmd(struct ql_system *sys);
void
ql_system_remove_hmd(struct ql_system *sys);

void
ql_system_reference(struct ql_system **dst, struct ql_system *src);

#ifdef __cplusplus
}
#endif

#endif
