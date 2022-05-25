// Copyright 2021-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  D3D 11 and 12 shared routines
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup oxr_main
 */

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "d3d/d3d_dxgi_helpers.hpp"

#include "oxr_objects.h"
#include "oxr_logger.h"

#include <dxgi1_6.h>
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
oxr_d3d_get_requirements(struct oxr_logger *log,
                         struct oxr_system *sys,
                         LUID *adapter_luid,
                         D3D_FEATURE_LEVEL *min_feature_level)
{
	try {

		if (sys->xsysc->info.client_d3d_deviceLUID_valid) {
			sys->suggested_d3d_luid =
			    reinterpret_cast<const LUID &>(sys->xsysc->info.client_d3d_deviceLUID);
			if (nullptr == getAdapterByLUID(sys->xsysc->info.client_d3d_deviceLUID, U_LOGGING_INFO)) {
				return oxr_error(log, XR_ERROR_RUNTIME_FAILURE,
				                 " failure enumerating adapter for LUID specified for use.");
			}
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
		*adapter_luid = sys->suggested_d3d_luid;
		//! @todo implement better?
		*min_feature_level = D3D_FEATURE_LEVEL_11_0;

		return XR_SUCCESS;
	}
	DEFAULT_CATCH(" failure determining adapter LUID")
}

XrResult
oxr_d3d_check_luid(struct oxr_logger *log, struct oxr_system *sys, LUID *adapter_luid)
{
	if (adapter_luid->HighPart != sys->suggested_d3d_luid.HighPart ||
	    adapter_luid->LowPart != sys->suggested_d3d_luid.LowPart) {

		return oxr_error(log, XR_ERROR_GRAPHICS_DEVICE_INVALID,
		                 " supplied device does not match required LUID.");
	}
	return XR_SUCCESS;
}
