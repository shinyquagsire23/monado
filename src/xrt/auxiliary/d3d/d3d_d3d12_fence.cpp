// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief D3D12-backed fence (timeline semaphore) creation routine
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#include "d3d_d3d12_fence.hpp"

#include "util/u_misc.h"
#include "util/u_logging.h"
#include "util/u_debug.h"
#include "util/u_handles.h"
#include "util/u_time.h"

#include "xrt/xrt_windows.h"

#include <Unknwn.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <wil/com.h>
#include <wil/result.h>

#include <inttypes.h>
#include <memory>

using namespace std::chrono;


#define DEFAULT_CATCH(...)                                                                                             \
	catch (wil::ResultException const &e)                                                                          \
	{                                                                                                              \
		U_LOG_E("Caught exception: %s", e.what());                                                             \
		return __VA_ARGS__;                                                                                    \
	}                                                                                                              \
	catch (std::exception const &e)                                                                                \
	{                                                                                                              \
		U_LOG_E("Caught exception: %s", e.what());                                                             \
		return __VA_ARGS__;                                                                                    \
	}                                                                                                              \
	catch (...)                                                                                                    \
	{                                                                                                              \
		U_LOG_E("Caught exception");                                                                           \
		return __VA_ARGS__;                                                                                    \
	}


namespace xrt::auxiliary::d3d::d3d12 {

xrt_result_t
createSharedFence(ID3D12Device &device,
                  bool share_cross_adapter,
                  xrt_graphics_sync_handle_t *out_handle,
                  wil::com_ptr<ID3D12Fence> &out_d3dfence)
try {
	/*
	 * Create the fence.
	 */

	D3D12_FENCE_FLAGS flags = share_cross_adapter ? D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER : D3D12_FENCE_FLAG_SHARED;

	wil::com_ptr<ID3D12Fence> fence;
	THROW_IF_FAILED(device.CreateFence( //
	    0,                              // InitialValue
	    flags,                          // Flags
	    __uuidof(ID3D12Fence),          // ReturnedInterface
	    fence.put_void()));             // ppFence

	/*
	 * Create the handle to be shared.
	 */

	DWORD access_flags = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;

	wil::unique_handle handle;
	THROW_IF_FAILED(device.CreateSharedHandle( //
	    fence.get(),                           // pOjbect
	    nullptr,                               // pAttributes
	    access_flags,                          // dwAccess
	    nullptr,                               // lpName
	    handle.put()));                        // pHandle


	/*
	 * Done.
	 */

	*out_handle = handle.release();
	out_d3dfence = std::move(fence);

	return XRT_SUCCESS;
}
DEFAULT_CATCH(XRT_ERROR_ALLOCATION)

xrt_result_t
waitOnFenceWithTimeout(wil::com_ptr<ID3D12Fence> fence,
                       wil::unique_event_nothrow &event,
                       uint64_t value,
                       std::chrono::milliseconds timeout_ms)
{
	DWORD dwTimeout = duration_cast<duration<DWORD, std::milli>>(timeout_ms).count();
	// Have to use this instead of ID3D12DeviceContext4::Wait because the latter has no timeout
	// parameter.
	fence->SetEventOnCompletion(value, event.get());
	if (value <= fence->GetCompletedValue()) {
		// oh we already reached this value.
		return XRT_SUCCESS;
	}
	if (event.wait(dwTimeout)) {
		return XRT_SUCCESS;
	}
	return XRT_TIMEOUT;
}

} // namespace xrt::auxiliary::d3d::d3d12
