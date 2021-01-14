// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Lighthouse base station control tools.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_config_have.h"

#include "os/os_ble.h"

#include "cli_common.h"

#include <string.h>
#include <stdio.h>


#define P(...) fprintf(stderr, __VA_ARGS__)

int
cli_cmd_lighthouse(int argc, const char **argv)
{
#ifdef XRT_HAVE_DBUS
	if (argc <= 2) {
		fprintf(stderr, "Command needs [on|off] argument!\n");
		return -1;
	}

	uint8_t value = 0;
	const char *str = NULL;
	if (strcmp(argv[2], "on") == 0) {
		value = 1;
		str = "on";
	} else if (strcmp(argv[2], "off") == 0) {
		value = 0;
		str = "off";
	} else {
		P("Command needs [on|off] argument != '%s'!\n", argv[2]);
		return -1;
	}

	P("Turning lighthouse %s!\n", str);
	os_ble_broadcast_write_value("00001523-1212-efde-1523-785feabcd124", "00001525-1212-efde-1523-785feabcd124",
	                             value);
	return 0;
#else
	P("Command needs bluetooth support!\n");
	return -1;
#endif
}
