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
 * @brief  Translation layer from XRSP controller samples to OpenXR
 *
 * Glue code from sampled XRSP poses/button bitfields to OpenXR inputs,
 * haptics, and base poses.
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

struct ql_controller *
ql_controller_create(struct ql_system *sys, enum xrt_device_type device_type);

#ifdef __cplusplus
}
#endif
