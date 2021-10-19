// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining a D3D11 graphics interface
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_config_os.h"
#include "xrt/xrt_windows.h"

#if defined(XRT_OS_WINDOWS)
#include "d3d11.h"
#elif defined(XRT_DOXYGEN)
struct ID3D11Device;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(XRT_OS_WINDOWS) || defined(XRT_DOXYGEN)

/*!
 * Create a D3D11 compositor client.
 *
 * @ingroup xrt_iface
 * @public @memberof xrt_compositor_native
 */
struct xrt_compositor_d3d11 *
xrt_gfx_d3d11_provider_create(struct xrt_compositor_native *xcn, ID3D11Device *device);

#endif // XRT_OS_WINDOWS || XRT_DOXYGEN

#ifdef __cplusplus
}
#endif
