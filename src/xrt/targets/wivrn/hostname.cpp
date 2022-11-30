// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#include "hostname.h"

#include "xrt/xrt_config_os.h"

#if defined(XRT_OS_APPLE) || defined(XRT_OS_LINUX)
#include <unistd.h>

std::string
hostname()
{
	char tmp[256];

	if (gethostname(tmp, sizeof(tmp)) < 0) {
		return "Unknown";
	}

	return std::string(tmp);
	//return "WiVRn-Hostname";
}

#else

#include <systemd/sd-bus.h>

std::string
hostname()
{
	sd_bus *bus;
	if (sd_bus_default_system(&bus) < 0)
		return "";

	for (auto property : {"PrettyHostname", "StaticHostname", "Hostname"}) {
		char *hostname = nullptr;
		sd_bus_error error = SD_BUS_ERROR_NULL;

		if (sd_bus_get_property_string(bus, "org.freedesktop.hostname1", "/org/freedesktop/hostname1",
		                               "org.freedesktop.hostname1", property, &error, &hostname) < 0) {
			sd_bus_error_free(&error);
			continue;
		}

		sd_bus_error_free(&error);
		if (hostname && strcmp(hostname, "")) {
			std::string s = hostname;
			free(hostname);
			sd_bus_unref(bus);
			return s;
		}
	}

	sd_bus_unref(bus);

	return "Unknown";
}
#endif
