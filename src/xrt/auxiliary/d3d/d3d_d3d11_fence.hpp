// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief D3D11-backed fence (timeline semaphore) creation routine.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#pragma once

#include "xrt/xrt_compositor.h"

#include <Unknwn.h>
#include <d3d11_4.h>
#include <wil/com.h>


namespace xrt::auxiliary::d3d {

/**
 * Allocate a fence (ID3D11Fence) that has a corresponding native handle.
 *
 * D3D fences are roughly equivalent to Vulkan timeline semaphores.
 *
 * @param device A D3D device to allocate with.
 * @param share_cross_adapter True if the fence should be shared across adapters, not only across ID3D11Device
 * instances.
 * @param[out] out_handle A graphics sync handle to populate
 * @param[out] out_d3dfence A COM pointer to the D3D11 fence to populate
 *
 * @return xrt_result_t, one of:
 * - @ref XRT_SUCCESS
 * - @ref XRT_ERROR_ALLOCATION
 * - @ref XRT_ERROR_D3D11
 */
xrt_result_t
createSharedFence(ID3D11Device5 &device,
                  bool share_cross_adapter,
                  xrt_graphics_sync_handle_t *out_handle,
                  wil::com_ptr<ID3D11Fence> &out_d3dfence);

}; // namespace xrt::auxiliary::d3d
