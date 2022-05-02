// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Misc D3D11 helper routines.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#include "d3d_helpers.hpp"

#include "util/u_logging.h"

#include <dxgi1_6.h>
#include <d3d11_4.h>
#include <wil/com.h>
#include <wil/result.h>

#include <vector>

namespace xrt::auxiliary::d3d {
template <typename T>
static wil::com_ptr<T>
try_create_dxgi_factory()
{

	wil::com_ptr<T> factory;
	LOG_IF_FAILED(CreateDXGIFactory1(__uuidof(T), factory.put_void()));

	return factory;
}

wil::com_ptr<IDXGIAdapter>
getAdapterByIndex(uint16_t index, u_logging_level log_level)
{

	wil::com_ptr<IDXGIAdapter> ret;
	auto factory6 = try_create_dxgi_factory<IDXGIFactory6>();
	if (factory6 != nullptr) {
		U_LOG_IFL_I(log_level, "Using IDXGIFactory6::EnumAdapterByGpuPreference to select adapter to use.");
		LOG_IF_FAILED(factory6->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
		                                                   __uuidof(IDXGIAdapter), ret.put_void()));
		if (ret) {
			return ret;
		}
		// Otherwise fall through to the other factory
	}

	auto factory = try_create_dxgi_factory<IDXGIFactory>();
	if (factory != nullptr) {
		U_LOG_IFL_I(log_level,
		            "IDXGIFactory6 unavailable, using IDXGIFactory::EnumAdapters to select adapter to use.");
		LOG_IF_FAILED(factory->EnumAdapters(index, ret.put()));
		return ret;
	}
	return ret;
}

wil::com_ptr<IDXGIAdapter>
getAdapterByLUID(const xrt_luid_t &luid, u_logging_level log_level)
{
	LUID realLuid = reinterpret_cast<const LUID &>(luid);
	wil::com_ptr<IDXGIAdapter> ret;
	auto factory4 = try_create_dxgi_factory<IDXGIFactory4>();
	if (factory4 != nullptr) {
		U_LOG_IFL_I(log_level, "Using IDXGIFactory4::EnumAdapterByLuid to select adapter to use.");
		LOG_IF_FAILED(factory4->EnumAdapterByLuid(realLuid, __uuidof(IDXGIAdapter), ret.put_void()));
		if (ret) {
			return ret;
		}
		// Otherwise fall through to the other factory
	}

	// This basically is manual implementation of EnumAdapterByLuid
	auto factory1 = try_create_dxgi_factory<IDXGIFactory1>();
	if (factory1 != nullptr) {
		U_LOG_IFL_I(log_level,
		            "IDXGIFactory6 unavailable, using IDXGIFactory1::EnumAdapters1 to select adapter to use.");
		for (unsigned int i = 0;; ++i) {

			wil::com_ptr<IDXGIAdapter1> adapter;
			if (!SUCCEEDED(factory1->EnumAdapters1(i, adapter.put()))) {
				U_LOG_IFL_W(log_level,
				            "Ran out of adapters using IDXGIFactory1::EnumAdapters1 before finding a "
				            "matching LUID.");
				break;
			}
			DXGI_ADAPTER_DESC1 desc{};
			if (!SUCCEEDED(adapter->GetDesc1(&desc))) {
				continue;
			}
			if (realLuid.HighPart == desc.AdapterLuid.HighPart &&
			    realLuid.LowPart == desc.AdapterLuid.LowPart) {
				ret = adapter;
				break;
			}
		}
	}
	return ret;
}

HRESULT
tryCreateD3D11Device(const wil::com_ptr<IDXGIAdapter> &adapter,
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
createD3D11Device(const wil::com_ptr<IDXGIAdapter> &adapter, u_logging_level log_level)
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
	HRESULT hr = tryCreateD3D11Device(adapter, driver_type, creation_flags, feature_levels, device, context);
#ifndef NDEBUG
	if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
		U_LOG_IFL_D(log_level, "Removing the debug layer flag: not successful.");
		creation_flags &= ~D3D11_CREATE_DEVICE_DEBUG;
		hr = tryCreateD3D11Device(adapter, driver_type, creation_flags, feature_levels, device, context);
	}
#endif
	THROW_IF_FAILED(hr);
	return {device, context};
}
} // namespace xrt::auxiliary::d3d
