// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief D3D11-backed fence (timeline semaphore) creation routine
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#include "d3d_d3d11_fence.hpp"

#include "util/u_misc.h"
#include "util/u_logging.h"
#include "util/u_debug.h"
#include "util/u_handles.h"

#include "xrt/xrt_windows.h"

#include <Unknwn.h>
#include <d3d11_3.h>
#include <wil/com.h>
#include <wil/result.h>

#include <inttypes.h>
#include <memory>


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


namespace xrt::auxiliary::d3d {

xrt_result_t
createSharedFence(ID3D11Device5 &device,
                  bool share_cross_adapter,
                  xrt_graphics_sync_handle_t *out_handle,
                  wil::com_ptr<ID3D11Fence> &out_d3dfence)
try {
	/*
	 * Create the fence.
	 */

	D3D11_FENCE_FLAG flags = share_cross_adapter ? D3D11_FENCE_FLAG_SHARED_CROSS_ADAPTER : D3D11_FENCE_FLAG_SHARED;

	wil::com_ptr<ID3D11Fence> fence;
	THROW_IF_FAILED(device.CreateFence( //
	    0,                              // InitialValue
	    flags,                          // Flags
	    __uuidof(ID3D11Fence),          // ReturnedInterface
	    fence.put_void()));             // ppFence

	wil::com_ptr<IDXGIResource1> dxgiRes;
	fence.query_to(dxgiRes.put());


	/*
	 * Create the handle to be shared.
	 */

	DWORD access_flags = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;

	wil::unique_handle handle;
	THROW_IF_FAILED(dxgiRes->CreateSharedHandle( //
	    nullptr,                                 // pAttributes
	    access_flags,                            // dwAccess
	    nullptr,                                 // lpName
	    handle.put()));                          // pHandle


	/*
	 * Done.
	 */

	*out_handle = handle.release();
	out_d3dfence = std::move(fence);

	return XRT_SUCCESS;
}
DEFAULT_CATCH(XRT_ERROR_ALLOCATION)

} // namespace xrt::auxiliary::d3d
