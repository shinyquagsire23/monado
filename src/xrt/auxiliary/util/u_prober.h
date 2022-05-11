// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Helpers for prober related code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include "xrt/xrt_prober.h"


#ifdef __cplusplus
extern "C" {
#endif


/*!
 * Helper to match various strings of a xrt_prober_device to
 * @public @memberof xrt_prober
 */
bool
u_prober_match_string(struct xrt_prober *xp,
                      struct xrt_prober_device *dev,
                      enum xrt_prober_string type,
                      const char *to_match);

const char *
u_prober_string_to_string(enum xrt_prober_string t);

const char *
u_prober_bus_type_to_string(enum xrt_bus_type t);

bool
u_prober_match_string(struct xrt_prober *xp,
                      struct xrt_prober_device *dev,
                      enum xrt_prober_string type,
                      const char *to_match);


#ifdef __cplusplus
}
#endif
