// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to direct OSVR HDK driver code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


struct xrt_prober*
hdk_create_prober();


#ifdef __cplusplus
}
#endif
