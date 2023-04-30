// Copyright 2020-2021, N Madsen.
// Copyright 2020-2021, Collabora, Ltd.
// Copyright 2021-2023, Jan Schmidt
// SPDX-License-Identifier: BSL-1.0
//
/*!
 * @file
 * @brief Implementation of Original & HP WMR controllers
 * @author Jan Schmidt <jan@centricular.com>
 * @author Nis Madsen <nima_zero_one@protonmail.com>
 * @ingroup drv_wmr
 */
#pragma once

#include "wmr_controller_base.h"

struct wmr_controller_base *
wmr_controller_create(struct wmr_controller_connection *conn,
                      enum xrt_device_type controller_type,
                      uint16_t vid,
                      uint16_t pid,
                      enum u_logging_level log_level);
