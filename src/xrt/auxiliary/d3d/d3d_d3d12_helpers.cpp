// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Misc D3D12 helper routines.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#include "d3d_d3d12_helpers.hpp"
#include "d3d_d3d12_bits.h"

#include "util/u_logging.h"

#include <d3d12.h>
#include <wil/com.h>
#include <wil/result.h>

#include <vector>
#include <stdexcept>

namespace xrt::auxiliary::d3d::d3d12 {

wil::com_ptr<ID3D12Device>
createDevice(const wil::com_ptr<IDXGIAdapter> &adapter, u_logging_level log_level)
{
	if (adapter) {
		U_LOG_IFL_D(log_level, "Adapter provided.");
	}

	wil::com_ptr<ID3D12Device> device;
	THROW_IF_FAILED(
	    D3D12CreateDevice(wil::com_raw_ptr(adapter), D3D_FEATURE_LEVEL_11_1, IID_PPV_ARGS(device.put())));

	return device;
}


HRESULT
createCommandLists(ID3D12Device &device,
                   ID3D12CommandAllocator &command_allocator,
                   ID3D12Resource &resource,
                   enum xrt_swapchain_usage_bits bits,
                   wil::com_ptr<ID3D12CommandList> out_acquire_command_list,
                   wil::com_ptr<ID3D12CommandList> out_release_command_list)
{

	//! @todo do we need to set queue access somehow?
	wil::com_ptr<ID3D12GraphicsCommandList> acquireCommandList;
	RETURN_IF_FAILED(device.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, &command_allocator, nullptr,
	                                          IID_PPV_ARGS(acquireCommandList.put())));

	D3D12_RESOURCE_STATES appResourceState = d3d_convert_usage_bits_to_d3d12_app_resource_state(bits);

	/// @todo No idea if this is right, might depend on whether it's the compute or graphics compositor!
	D3D12_RESOURCE_STATES compositorResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = &resource;
	barrier.Transition.StateBefore = compositorResourceState;
	barrier.Transition.StateAfter = appResourceState;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	acquireCommandList->ResourceBarrier(1, &barrier);
	RETURN_IF_FAILED(acquireCommandList->Close());

	wil::com_ptr<ID3D12GraphicsCommandList> releaseCommandList;
	RETURN_IF_FAILED(device.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, &command_allocator, nullptr,
	                                          IID_PPV_ARGS(releaseCommandList.put())));
	barrier.Transition.StateBefore = appResourceState;
	barrier.Transition.StateAfter = compositorResourceState;

	releaseCommandList->ResourceBarrier(1, &barrier);
	RETURN_IF_FAILED(releaseCommandList->Close());

	out_acquire_command_list = std::move(acquireCommandList);
	out_release_command_list = std::move(releaseCommandList);
	return S_OK;
}


wil::com_ptr<ID3D12Resource>
importImage(ID3D12Device &device, HANDLE h)
{
	wil::com_ptr<ID3D12Resource> tex;

	if (h == nullptr) {
		throw std::logic_error("Cannot import empty handle");
	}
	THROW_IF_FAILED(device.OpenSharedHandle(h, IID_PPV_ARGS(tex.put())));
	return tex;
}


wil::com_ptr<ID3D12Fence1>
importFence(ID3D12Device &device, HANDLE h)
{
	wil::com_ptr<ID3D12Fence1> fence;

	if (h == nullptr) {
		throw std::logic_error("Cannot import empty handle");
	}
	THROW_IF_FAILED(device.OpenSharedHandle(h, IID_PPV_ARGS(fence.put())));
	return fence;
}

} // namespace xrt::auxiliary::d3d::d3d12
