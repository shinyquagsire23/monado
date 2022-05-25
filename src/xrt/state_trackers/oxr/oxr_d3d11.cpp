// Copyright 2021-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds D3D11 related functions that didn't fit somewhere else.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup oxr_main
 */

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

XrResult
oxr_d3d11_get_requirements(struct oxr_logger *log,
                           struct oxr_system *sys,
                           XrGraphicsRequirementsD3D11KHR *graphicsRequirements)
{
	return oxr_d3d_get_requirements(log, sys, &graphicsRequirements->adapterLuid,
	                                &graphicsRequirements->minFeatureLevel);
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
		return oxr_d3d_check_luid(log, sys, &desc.AdapterLuid);
	}
	DEFAULT_CATCH(" failure checking adapter LUID")
}
