// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Misc D3D11/12 helper routines.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_d3d
 */

#pragma once

#include "xrt/xrt_defines.h"

#include "util/u_logging.h"

#include <dxgi.h>
#include <wil/com.h>

#include <utility>


namespace xrt::auxiliary::d3d {

/**
 * @brief Create a DXGI Adapter, using our priorities.
 *
 * We try to use IDXGIFactory6::EnumAdapterByGpuPreference preferring HIGH_PERFORMANCE, if it's available
 *
 * @param index The requested adapter index
 * @param log_level The level to compare against for internal log messages
 *
 * @throws wil::ResultException in case of error
 *
 * @return wil::com_ptr<IDXGIAdapter>
 */
wil::com_ptr<IDXGIAdapter>
getAdapterByIndex(uint16_t index, u_logging_level log_level = U_LOGGING_INFO);

/**
 * @brief Create a DXGI Adapter, for the provided LUID.
 *
 * @param luid The requested adapter luid
 * @param log_level The level to compare against for internal log messages
 * @throws wil::ResultException in case of error
 *
 * @return wil::com_ptr<IDXGIAdapter>
 */
wil::com_ptr<IDXGIAdapter>
getAdapterByLUID(const xrt_luid_t &luid, u_logging_level log_level = U_LOGGING_INFO);

} // namespace xrt::auxiliary::d3d
