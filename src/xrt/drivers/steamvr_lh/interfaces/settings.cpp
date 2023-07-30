// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR IVRSettings interface implementation.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#include <optional>
#include <cstring>

#include "settings.hpp"
#include "util/u_json.hpp"

using xrt::auxiliary::util::json::JSONNode;

Settings::Settings(const std::string &steam_install, const std::string &steamvr_install)
    : steamvr_settings(JSONNode::loadFromFile(steam_install + "/config/steamvr.vrsettings")),
      driver_defaults(
          JSONNode::loadFromFile(steamvr_install + "/drivers/lighthouse/resources/settings/default.vrsettings"))
{}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
const char *
Settings::GetSettingsErrorNameFromEnum(vr::EVRSettingsError eError)
{
	return nullptr;
}

void
Settings::SetBool(const char *pchSection, const char *pchSettingsKey, bool bValue, vr::EVRSettingsError *peError)
{}

void
Settings::SetInt32(const char *pchSection, const char *pchSettingsKey, int32_t nValue, vr::EVRSettingsError *peError)
{}

void
Settings::SetFloat(const char *pchSection, const char *pchSettingsKey, float flValue, vr::EVRSettingsError *peError)
{}

void
Settings::SetString(const char *pchSection,
                    const char *pchSettingsKey,
                    const char *pchValue,
                    vr::EVRSettingsError *peError)
{}

bool
Settings::GetBool(const char *pchSection, const char *pchSettingsKey, vr::EVRSettingsError *peError)
{
	return false;
}

int32_t
Settings::GetInt32(const char *pchSection, const char *pchSettingsKey, vr::EVRSettingsError *peError)
{
	return 0;
}

float
Settings::GetFloat(const char *pchSection, const char *pchSettingsKey, vr::EVRSettingsError *peError)
{
	return 0.0;
}

// Driver requires a few string settings to initialize properly
void
Settings::GetString(const char *pchSection,
                    const char *pchSettingsKey,
                    char *pchValue,
                    uint32_t unValueLen,
                    vr::EVRSettingsError *peError)
{
	if (peError)
		*peError = vr::VRSettingsError_None;

	auto get_string = [pchSection, pchSettingsKey](const JSONNode &root) -> std::optional<std::string> {
		JSONNode section = root[pchSection];
		if (!section.isValid())
			return std::nullopt;

		JSONNode value = section[pchSettingsKey];
		if (!value.isValid() || !value.isString())
			return std::nullopt;

		return std::optional(value.asString());
	};

	std::optional value = get_string(driver_defaults);
	if (!value.has_value())
		value = get_string(steamvr_settings);

	if (value.has_value()) {
		if (unValueLen > value->size())
			std::strncpy(pchValue, value->c_str(), value->size() + 1);
	} else if (peError)
		*peError = vr::VRSettingsError_ReadFailed;
}

void
Settings::RemoveSection(const char *pchSection, vr::EVRSettingsError *peError)
{}

void
Settings::RemoveKeyInSection(const char *pchSection, const char *pchSettingsKey, vr::EVRSettingsError *peError)
{}
// NOLINTEND(bugprone-easily-swappable-parameters)
