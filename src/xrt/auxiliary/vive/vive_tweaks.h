// Copyright 2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Tweaks for various bits on Vive and Index headsets.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_vive
 */

#pragma once

#include "xrt/xrt_compiler.h"


#ifdef __cplusplus
extern "C" {
#endif

struct vive_config;


/*!
 * Tweak the fov for the views on the given config, to make it better.
 *
 * @ingroup aux_vive
 */
void
vive_tweak_fov(struct vive_config *config);


#ifdef __cplusplus
} // extern "C"
#endif
