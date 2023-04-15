// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR IVRDriverManager interface header.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#pragma once

#include "openvr_driver.h"

class DriverManager : public vr::IVRDriverManager
{
public:
	uint32_t
	GetDriverCount() const override;

	uint32_t
	GetDriverName(vr::DriverId_t nDriver, VR_OUT_STRING() char *pchValue, uint32_t unBufferSize) override;

	vr::DriverHandle_t
	GetDriverHandle(const char *pchDriverName) override;

	bool
	IsEnabled(vr::DriverId_t nDriver) const override;
};
