/*
 * Copyright 2013, Fredrik Hultin.
 * Copyright 2013, Jakob Bornecrantz.
 * Copyright 2016 Philipp Zabel
 * Copyright 2019-2022 Jan Schmidt
 * Copyright 2022  Guillaume Meunier
 * Copyright 2022  Patrick Nicolas
 * Copyright 2022-2023 Max Thomas
 * SPDX-License-Identifier: BSL-1.0
 *
 */
/*!
 * @file
 * @brief  Translation layer from XRSP HMD pose samples to OpenXR
 *
 * Glue code from sampled XRSP HMD poses OpenXR poses.
 * Includes distortion meshes for AADT.
 *
 * @author Max Thomas <mtinc2@gmail.com>
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

void ql_hmd_set_per_eye_resolution(struct ql_hmd* hmd, uint32_t w, uint32_t h, float fps);

struct ql_hmd *
ql_hmd_create(struct ql_system *sys, const unsigned char *hmd_serial_no);
//void
//ql_hmd_handle_report(struct ql_hmd *hmd, timepoint_ns local_ts, ql_hmd_report_t *report);
void
ql_hmd_set_proximity(struct ql_hmd *hmd, bool prox_sensor);

#ifdef __cplusplus
}
#endif

#endif
