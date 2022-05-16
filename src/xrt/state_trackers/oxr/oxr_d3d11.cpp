// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds D3D11 related functions that didn't fit somewhere else.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup oxr_main
 */

#include "xrt/xrt_gfx_d3d11.h"

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "d3d/d3d_helpers.hpp"

#include "oxr_objects.h"
#include "oxr_logger.h"

#include <dxgi1_6.h>
#include <d3d11_4.h>
#include <wil/com.h>
#include <wil/result.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DEFAULT_CATCH(MSG)                                                                                             \
	catch (wil::ResultException const &e)                                                                          \
	{                                                                                                              \
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, MSG ": %s", e.what());                                 \
	}                                                                                                              \
	catch (std::exception const &e)                                                                                \
	{                                                                                                              \
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, MSG ": %s", e.what());                                 \
	}                                                                                                              \
	catch (...)                                                                                                    \
	{                                                                                                              \
		return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, MSG);                                                  \
	}

using namespace xrt::auxiliary::d3d;

XrResult
oxr_d3d11_get_requirements(struct oxr_logger *log,
                           struct oxr_system *sys,
                           XrGraphicsRequirementsD3D11KHR *graphicsRequirements)
{
	try {

		if (sys->xsysc->info.client_d3d_deviceLUID_valid) {
			sys->suggested_d3d_luid =
			    reinterpret_cast<const LUID &>(sys->xsysc->info.client_d3d_deviceLUID);
		} else {
			auto adapter = getAdapterByIndex(0, U_LOGGING_INFO);
			if (adapter == nullptr) {
				return oxr_error(log, XR_ERROR_RUNTIME_FAILURE, " failure enumerating adapter LUIDs.");
			}
			DXGI_ADAPTER_DESC desc{};
			THROW_IF_FAILED(adapter->GetDesc(&desc));
			sys->suggested_d3d_luid = desc.AdapterLuid;
		}
		sys->suggested_d3d_luid_valid = true;
		graphicsRequirements->adapterLuid = sys->suggested_d3d_luid;
		//! @todo implement better?
		graphicsRequirements->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;

		return XR_SUCCESS;
	}
	DEFAULT_CATCH(" failure determining adapter LUID")
}

XrResult
oxr_d3d11_check_device(struct oxr_logger *log, struct oxr_system *sys, ID3D11Device *device)
{
	try {
		wil::com_ptr<IDXGIDevice> dxgiDevice;
		THROW_IF_FAILED(device->QueryInterface(dxgiDevice.put()));
		wil::com_ptr<IDXGIAdapter> adapter;
		THROW_IF_FAILED(dxgiDevice->GetAdapter(adapter.put()));
		DXGI_ADAPTER_DESC desc{};
		adapter->GetDesc(&desc);
		if (desc.AdapterLuid.HighPart != sys->suggested_d3d_luid.HighPart ||
		    desc.AdapterLuid.LowPart != sys->suggested_d3d_luid.LowPart) {

			return oxr_error(log, XR_ERROR_GRAPHICS_DEVICE_INVALID,
			                 " supplied device does not match required LUID.");
		}
		return XR_SUCCESS;
	}
	DEFAULT_CATCH(" failure checking adapter LUID")
}
