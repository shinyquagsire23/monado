/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * SPDX-License-Identifier: BSL-1.0
 */

/*!
 * @file
 * @brief  Interface to the Meta Quest Link HMD driver code.
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_quest_link
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "math/m_imu_3dof.h"
#include "util/u_distortion_mesh.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "ql_types.h"

/* Meta Quest Link HMD Internal Interface */
#ifndef QUEST_LINK_HMD_H
#define QUEST_LINK_HMD_H

struct ql_hmd *
ql_hmd_create(struct ql_system *sys, const unsigned char *hmd_serial_no, struct ql_hmd_config *config);
//void
//ql_hmd_handle_report(struct ql_hmd *hmd, timepoint_ns local_ts, ql_hmd_report_t *report);
void
ql_hmd_set_proximity(struct ql_hmd *hmd, bool prox_sensor);

#ifdef __cplusplus
}
#endif

#endif
