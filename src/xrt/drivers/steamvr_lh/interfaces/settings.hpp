// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR IVRSettings interface header.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#pragma once

#include "openvr_driver.h"
#include "util/u_json.hpp"

class Settings : public vr::IVRSettings
{
private:
	const xrt::auxiliary::util::json::JSONNode steamvr_settings;
	const xrt::auxiliary::util::json::JSONNode driver_defaults;

public:
	Settings(const std::string &steam_install, const std::string &steamvr_install);

	const char *
	GetSettingsErrorNameFromEnum(vr::EVRSettingsError eError) override;

	void
	SetBool(const char *pchSection,
	        const char *pchSettingsKey,
	        bool bValue,
	        vr::EVRSettingsError *peError = nullptr) override;
	void
	SetInt32(const char *pchSection,
	         const char *pchSettingsKey,
	         int32_t nValue,
	         vr::EVRSettingsError *peError = nullptr) override;
	void
	SetFloat(const char *pchSection,
	         const char *pchSettingsKey,
	         float flValue,
	         vr::EVRSettingsError *peError = nullptr) override;
	void
	SetString(const char *pchSection,
	          const char *pchSettingsKey,
	          const char *pchValue,
	          vr::EVRSettingsError *peError = nullptr) override;

	// Users of the system need to provide a proper default in default.vrsettings in the resources/settings/
	// directory of either the runtime or the driver_xxx directory. Otherwise the default will be false, 0, 0.0 or
	// ""
	bool
	GetBool(const char *pchSection, const char *pchSettingsKey, vr::EVRSettingsError *peError = nullptr) override;
	int32_t
	GetInt32(const char *pchSection, const char *pchSettingsKey, vr::EVRSettingsError *peError = nullptr) override;
	float
	GetFloat(const char *pchSection, const char *pchSettingsKey, vr::EVRSettingsError *peError = nullptr) override;
	void
	GetString(const char *pchSection,
	          const char *pchSettingsKey,
	          VR_OUT_STRING() char *pchValue,
	          uint32_t unValueLen,
	          vr::EVRSettingsError *peError = nullptr) override;

	void
	RemoveSection(const char *pchSection, vr::EVRSettingsError *peError = nullptr) override;
	void
	RemoveKeyInSection(const char *pchSection,
	                   const char *pchSettingsKey,
	                   vr::EVRSettingsError *peError = nullptr) override;
};
