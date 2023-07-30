// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR IVRDriverManager interface implementation.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#include "driver_manager.hpp"

uint32_t
DriverManager::GetDriverCount() const
{
	return 1;
}

uint32_t
DriverManager::GetDriverName(vr::DriverId_t nDriver, VR_OUT_STRING() char *pchValue, uint32_t unBufferSize)
{
	return 0;
}

vr::DriverHandle_t
DriverManager::GetDriverHandle(const char *pchDriverName)
{
	return 1;
}

bool
DriverManager::IsEnabled(vr::DriverId_t nDriver) const
{
	return nDriver == 1;
}
