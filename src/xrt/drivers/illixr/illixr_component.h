// Copyright 2020-2021, The Board of Trustees of the University of Illinois.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  ILLIXR plugin
 * @author RSIM Group <illixr@cs.illinois.edu>
 * @ingroup drv_illixr
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void *
illixr_monado_create_plugin(void *pb);
struct xrt_pose
illixr_read_pose();

#ifdef __cplusplus
}
#endif
