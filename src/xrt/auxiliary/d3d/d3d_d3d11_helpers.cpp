// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Misc D3D11 helper routines.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#include "d3d_d3d11_helpers.hpp"

#include "util/u_logging.h"

#include <dxgi1_6.h>
#include <d3d11_4.h>
#include <wil/com.h>
#include <wil/result.h>

#include <vector>

namespace xrt::auxiliary::d3d::d3d11 {
HRESULT
tryCreateDevice(const wil::com_ptr<IDXGIAdapter> &adapter,
                D3D_DRIVER_TYPE driver_type,
                unsigned int creation_flags,
                const std::vector<D3D_FEATURE_LEVEL> &feature_levels,
                wil::com_ptr<ID3D11Device> &out_device,
                wil::com_ptr<ID3D11DeviceContext> &out_context)
{
	return D3D11CreateDevice(wil::com_raw_ptr(adapter), driver_type, nullptr, creation_flags, feature_levels.data(),
	                         (UINT)feature_levels.size(), D3D11_SDK_VERSION, out_device.put(), nullptr,
	                         out_context.put());
}

std::pair<wil::com_ptr<ID3D11Device>, wil::com_ptr<ID3D11DeviceContext>>
createDevice(const wil::com_ptr<IDXGIAdapter> &adapter, u_logging_level log_level)
{
	D3D_DRIVER_TYPE driver_type = D3D_DRIVER_TYPE_HARDWARE;
	if (adapter) {
		// needed if we pass an adapter.
		U_LOG_IFL_D(log_level, "Adapter provided.");
		driver_type = D3D_DRIVER_TYPE_UNKNOWN;
	}
	unsigned int creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
	U_LOG_IFL_D(log_level, "Will attempt to create our device using the debug layer.");
	creation_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	std::vector<D3D_FEATURE_LEVEL> feature_levels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
	wil::com_ptr<ID3D11Device> device;
	wil::com_ptr<ID3D11DeviceContext> context;
	HRESULT hr = tryCreateDevice(adapter, driver_type, creation_flags, feature_levels, device, context);
#ifndef NDEBUG
	if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
		U_LOG_IFL_D(log_level, "Removing the debug layer flag: not successful.");
		creation_flags &= ~D3D11_CREATE_DEVICE_DEBUG;
		hr = tryCreateDevice(adapter, driver_type, creation_flags, feature_levels, device, context);
	}
#endif
	THROW_IF_FAILED(hr);
	return {device, context};
}
} // namespace xrt::auxiliary::d3d::d3d11
