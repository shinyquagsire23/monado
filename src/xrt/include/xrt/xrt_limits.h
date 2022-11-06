// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header for limits of the XRT interfaces.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_compiler.h"


/*!
 * @addtogroup xrt_iface
 * @{
 */

/*!
 * Maximum number of handles sent in one call.
 */
#define XRT_MAX_IPC_HANDLES 16

/*!
 * Max swapchain images, artificial limit.
 *
 * Must be smaller or the same as XRT_MAX_IPC_HANDLES.
 */
#define XRT_MAX_SWAPCHAIN_IMAGES 8

/*!
 * Max formats supported by a compositor, artificial limit.
 */
#define XRT_MAX_SWAPCHAIN_FORMATS 16

/*!
 * @}
 */
