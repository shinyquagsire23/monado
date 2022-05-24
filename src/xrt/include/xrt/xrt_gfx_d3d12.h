// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header defining a D3D12 graphics interface
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup xrt_iface
 */

#pragma once

#include "xrt/xrt_device.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_windows.h"

#if defined(XRT_HAVE_D3D12)
#include "d3d12.h"
#elif defined(XRT_DOXYGEN)
struct ID3D12Device;
struct ID3D12CommandQueue;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(XRT_OS_WINDOWS) || defined(XRT_DOXYGEN)

/*!
 * Create a D3D12 compositor client.
 *
 * @ingroup xrt_iface
 * @public @memberof xrt_compositor_native
 */
struct xrt_compositor_d3d12 *
xrt_gfx_d3d12_provider_create(struct xrt_compositor_native *xcn, ID3D12Device *device, ID3D12CommandQueue *queue);

#endif // XRT_OS_WINDOWS || XRT_DOXYGEN

#ifdef __cplusplus
}
#endif
