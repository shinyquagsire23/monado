// Copyright 2023, Shawn Wallace
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief OpenVR IVRResources interface implementation.
 * @author Shawn Wallace <yungwallace@live.com>
 * @ingroup drv_steamvr_lh
 */

#include "resources.hpp"
#include "util/u_logging.h"
#include <cstring>

#define RES_ERR(...) U_LOG_IFL_E(log_level, __VA_ARGS__)
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
uint32_t
Resources::LoadSharedResource(const char *pchResourceName, char *pchBuffer, uint32_t unBufferLen)
{
	return 0;
}

uint32_t
Resources::GetResourceFullPath(const char *pchResourceName,
                               const char *pchResourceTypeDirectory,
                               char *pchPathBuffer,
                               uint32_t unBufferLen)
{
	std::string resource(pchResourceName);

	auto return_str = [&](const std::string &str) {
		const auto len = str.size() + 1;
		if (unBufferLen >= len) {
			std::strncpy(pchPathBuffer, str.c_str(), len);
		}
		return len;
	};
	// loading resource from driver folder (i.e. htc)
	if (resource[0] == '{') {
		const size_t idx = resource.find('}');
		if (idx == std::string::npos) {
			RES_ERR("malformed resource name: %s", resource.c_str());
			return 0;
		}
		const std::string driver = resource.substr(1, idx - 1);
		std::string path = steamvr_install + "/drivers/" + driver + "/resources/";
		if (pchResourceTypeDirectory)
			path += pchResourceTypeDirectory + std::string("/");

		// for some reason sometimes it gives the paths like {driver}resource.file instead of
		// {driver}/resource.file
		path += resource.substr(idx + 1);
		return return_str(path);
	}

	// loading from shared folder?
	std::string path = steamvr_install + "/resources/";
	path += pchResourceTypeDirectory;
	path += "/";
	path += pchResourceName;
	return return_str(path);
}
// NOLINTEND(bugprone-easily-swappable-parameters)
