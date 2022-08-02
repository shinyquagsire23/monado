// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Misc D3D12 helper routines.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#pragma once

#include "xrt/xrt_defines.h"
#include "xrt/xrt_compositor.h"

#include "util/u_logging.h"

#include <dxgi.h>
#include <d3d12.h>

#include <wil/com.h>

#include <utility>


namespace xrt::auxiliary::d3d::d3d12 {


/**
 * @brief Create a D3D12 Device object
 *
 * @param adapter optional: adapter to create on.
 * @param log_level The level to compare against for internal log messages
 *
 * @throws wil::ResultException in case of error
 *
 * @return wil::com_ptr<ID3D12Device>
 */
wil::com_ptr<ID3D12Device>
createDevice(const wil::com_ptr<IDXGIAdapter> &adapter = nullptr, u_logging_level log_level = U_LOGGING_INFO);

/**
 * @brief Create command lists for a resource transitioning to/from app control
 *
 * @param device D3D12 device
 * @param command_allocator
 * @param resource image
 * @param bits Swapchain usage bits
 * @param[out] out_acquire_command_list Command list to populate for xrAcquireSwapchainImage
 * @param[out] out_release_command_list Command list to populate for xrReleaseSwapchainImage
 * @return HRESULT
 */
HRESULT
createCommandLists(ID3D12Device &device,
                   ID3D12CommandAllocator &command_allocator,
                   ID3D12Resource &resource,
                   enum xrt_swapchain_usage_bits bits,
                   wil::com_ptr<ID3D12CommandList> out_acquire_command_list,
                   wil::com_ptr<ID3D12CommandList> out_release_command_list);

/**
 * Imports an image into D3D12 from a handle.
 *
 * @param device D3D12 device
 * @param h handle corresponding to a shared image
 *
 * @throw std::logic_error if the handle is invalid, wil exceptions if the operation failed.
 */
wil::com_ptr<ID3D12Resource>
importImage(ID3D12Device &device, HANDLE h);

/**
 * Imports a fence into D3D12 from a handle.
 *
 * @param device D3D12 device
 * @param h handle corresponding to a shared fence
 *
 * @throw std::logic_error if the handle is invalid, wil exceptions if the operation failed.
 */
wil::com_ptr<ID3D12Fence1>
importFence(ID3D12Device &device, HANDLE h);

} // namespace xrt::auxiliary::d3d::d3d12
