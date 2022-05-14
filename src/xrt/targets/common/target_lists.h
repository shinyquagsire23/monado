// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Common things to pull into a target.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#pragma once

#include "xrt/xrt_prober.h"

extern struct xrt_prober_entry target_entry_list[];
extern struct xrt_prober_entry *target_entry_lists[];
extern xrt_auto_prober_create_func_t target_auto_list[];
extern xrt_builder_create_func_t target_builder_list[];
extern struct xrt_prober_entry_lists target_lists;
