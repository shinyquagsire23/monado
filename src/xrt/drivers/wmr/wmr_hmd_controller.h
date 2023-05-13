// Copyright 2023 Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Implementation of tunnelled controller connection,
 * that translates messages passing via an HP G2 or Sasmung Odyssey+ HMD
 * @author Jan Schmidt <jan@centricular.com>
 * @ingroup drv_wmr
 */
#include <stdint.h>

#include "os/os_threading.h"
#include "xrt/xrt_device.h"

#include "wmr_controller_base.h"

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct wmr_hmd_controller_connection
{
	struct wmr_controller_connection base;

	/* Controller and HMD each hold a reference. It's
	 * only cleaned up once both release it. */
	struct xrt_reference ref;
	enum u_logging_level log_level;

	uint8_t hmd_cmd_base;

	/* Protect access when sending / receiving data */
	struct os_mutex lock;
	bool disconnected; /* Set to true once disconnect() is called */

	struct wmr_hmd *hmd;
};

struct wmr_hmd_controller_connection *
wmr_hmd_controller_create(struct wmr_hmd *hmd,
                          uint8_t hmd_cmd_base,
                          enum xrt_device_type controller_type,
                          uint16_t vid,
                          uint16_t pid,
                          enum u_logging_level log_level);

struct xrt_device *
wmr_hmd_controller_connection_get_controller(struct wmr_hmd_controller_connection *wcc);

#ifdef __cplusplus
}
#endif
