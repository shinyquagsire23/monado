// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared bindings structs for @ref drv_vive & @ref drv_survive.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_vive
 */

#pragma once

#include "xrt/xrt_device.h"


extern struct xrt_binding_profile vive_binding_profiles_index[];
extern struct xrt_binding_profile vive_binding_profiles_wand[];

extern uint32_t vive_binding_profiles_wand_count;
extern uint32_t vive_binding_profiles_index_count;
