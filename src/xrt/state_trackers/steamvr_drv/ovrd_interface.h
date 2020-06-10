// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Interface to the Monado SteamVR Driver exporter.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup st_ovrd
 */

#pragma once

#include "xrt/xrt_defines.h"

/*!
 * @defgroup st_ovrd SteamVR driver provider
 *
 * Wraps a @ref xrt_instance and one or more @ref xrt_device and exposes those
 * to SteamVR via the OpenVR driver interface.
 *
 * @ingroup xrt
 */

/*!
 * Implementation of the HmdDriverFactory function.
 *
 * @ingroup st_ovrd
 */
void *
ovrd_hmd_driver_impl(const char *pInterfaceName, int *pReturnCode);
