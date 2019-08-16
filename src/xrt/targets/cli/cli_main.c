// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A cli program to configure and test Monado.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include <string.h>
#include <stdio.h>

#include "cli_common.h"


#define P(...) fprintf(stderr, __VA_ARGS__)

static int
cli_print_help(int argc, const char **argv)
{
	if (argc >= 2) {
		fprintf(stderr, "Unknown command '%s'\n\n", argv[1]);
	}

	P("Monado-CLI 0.0.1\n");
	P("Usage: %s command [options]\n", argv[0]);
	P("\n");
	P("Commands:\n");
	P("  test       - List found devices, for prober testing.\n");
	P("  calibrate  - Calibrate a camera and save config.\n");

	return 1;
}

int
main(int argc, const char **argv)
{
	if (argc <= 1) {
		return cli_print_help(argc, argv);
	}

	if (strcmp(argv[1], "test") == 0) {
		return cli_cmd_test(argc, argv);
	}
	if (strcmp(argv[1], "calibrate") == 0) {
		return cli_cmd_calibrate(argc, argv);
	}
	return cli_print_help(argc, argv);
}
