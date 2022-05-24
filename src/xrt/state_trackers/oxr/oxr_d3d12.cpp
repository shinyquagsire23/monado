// Copyright 2021-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Holds D3D12 related functions that didn't fit somewhere else.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup oxr_main
 */

#include "util/u_misc.h"
#include "util/u_debug.h"
#include "d3d/d3d_d3d12_helpers.hpp"

#include "oxr_objects.h"
#include "oxr_logger.h"

#include <dxgi1_6.h>
#include <d3d12.h>
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
oxr_d3d12_get_requirements(struct oxr_logger *log,
                           struct oxr_system *sys,
                           XrGraphicsRequirementsD3D12KHR *graphicsRequirements)
{
	return oxr_d3d_get_requirements(log, sys, &graphicsRequirements->adapterLuid,
	                                &graphicsRequirements->minFeatureLevel);
}

XrResult
oxr_d3d12_check_device(struct oxr_logger *log, struct oxr_system *sys, ID3D12Device *device)
{
	try {
		LUID luid = device->GetAdapterLuid();
		return oxr_d3d_check_luid(log, sys, &luid);
	}
	DEFAULT_CATCH(" failure checking adapter LUID")
}
