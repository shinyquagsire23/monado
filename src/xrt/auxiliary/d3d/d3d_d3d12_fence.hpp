// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief D3D12-backed fence (timeline semaphore) creation routine.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#pragma once

#include "xrt/xrt_compositor.h"

#include <Unknwn.h>
#include <d3d12.h>
#include <wil/com.h>

#include <chrono>

namespace xrt::auxiliary::d3d::d3d12 {

/**
 * Allocate a fence (ID3D12Fence) that has a corresponding native handle.
 *
 * D3D fences are roughly equivalent to Vulkan timeline semaphores.
 *
 * @param device A D3D device to allocate with.
 * @param share_cross_adapter True if the fence should be shared across adapters, not only across ID3D12Device
 * instances.
 * @param[out] out_handle A graphics sync handle to populate
 * @param[out] out_d3dfence A COM pointer to the D3D12 fence to populate
 *
 * @return xrt_result_t, one of:
 * - @ref XRT_SUCCESS
 * - @ref XRT_ERROR_ALLOCATION
 * - @ref XRT_ERROR_D3D12
 */
xrt_result_t
createSharedFence(ID3D12Device &device,
                  bool share_cross_adapter,
                  xrt_graphics_sync_handle_t *out_handle,
                  wil::com_ptr<ID3D12Fence> &out_d3dfence);

/*!
 * Wait for a fence to be signaled with value equal or greater than @p value within @p timeout_ns nanoseconds.
 *
 * @param fence The fence to wait on.
 * @param event An event to use to wait. Please use a dedicated event for a single thread's calls to this function.
 * @param value The desired fence value
 * @param timeout_ms After this long, we may return early with @ref XRT_TIMEOUT even before the fence
 * reaches the value.
 */
xrt_result_t
waitOnFenceWithTimeout(wil::com_ptr<ID3D12Fence> fence,
                       wil::unique_event_nothrow &event,
                       uint64_t value,
                       std::chrono::milliseconds timeout_ms);

}; // namespace xrt::auxiliary::d3d::d3d12
