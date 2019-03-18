// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Misc helpers for device drivers.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#pragma once

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_device.h"

#ifdef __cplusplus
extern "C" {
#endif


extern const struct xrt_matrix_2x2 u_device_rotation_right;
extern const struct xrt_matrix_2x2 u_device_rotation_left;
extern const struct xrt_matrix_2x2 u_device_rotation_ident;
extern const struct xrt_matrix_2x2 u_device_rotation_180;

/*!
 * Dump the device config to stderr.
 */
void
u_device_dump_config(struct xrt_device* xdev,
                     const char* prefix,
                     const char* prod);


#ifdef __cplusplus
}
#endif
