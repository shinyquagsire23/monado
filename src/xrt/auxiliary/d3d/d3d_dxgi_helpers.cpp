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
} // namespace xrt::auxiliary::d3d
