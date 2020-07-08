// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Prints a list of found devices and tests opening some of them.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include <string.h>
#include <stdio.h>

#include "xrt/xrt_instance.h"
#include "xrt/xrt_prober.h"
#include "cli_common.h"


static int
do_exit(struct xrt_instance **xi_ptr, int ret)
{
	xrt_instance_destroy(xi_ptr);

	printf(" :: Exiting '%i'\n", ret);

	return ret;
}

#define NUM_XDEVS 32

int
cli_cmd_test(int argc, const char **argv)
{
	struct xrt_device *xdevs[NUM_XDEVS] = {0};
	struct xrt_instance *xi = NULL;
	int ret = 0;

	// Initialize the prober.
	printf(" :: Creating instance!\n");

	ret = xrt_instance_create(NULL, &xi);
	if (ret != 0) {
		return do_exit(&xi, 0);
	}
	struct xrt_prober *xp = NULL;

	ret = xrt_instance_get_prober(xi, &xp);
	if (ret != 0) {
		do_exit(&xi, ret);
	}
	if (xp != NULL) {
		// This instance provides an xrt_prober so we can dump some
		// internal info.

		// Need to prime the prober with devices before dumping and
		// listing.
		printf(" :: Probing!\n");

		ret = xrt_prober_probe(xp);
		if (ret != 0) {
			return do_exit(&xi, ret);
		}

		// So the user can see what we found.
		printf(" :: Dumping!\n");

		ret = xrt_prober_dump(xp);
		if (ret != 0) {
			do_exit(&xi, ret);
		}
	}

	// Regardless of whether xrt_prober is used, we can find and select
	// (multiple) devices.
	printf(" :: Probing and selecting devices!\n");

	ret = xrt_instance_select(xi, xdevs, NUM_XDEVS);
	if (ret != 0) {
		return do_exit(&xi, ret);
	}
	if (xdevs[0] == NULL) {
		printf("\tNo HMD found! :(\n");
		return do_exit(&xi, -1);
	}


	for (size_t i = 0; i < NUM_XDEVS; i++) {
		if (xdevs[i] == NULL) {
			continue;
		}

		printf("\tFound '%s'\n", xdevs[i]->str);
	}

	// End of program
	printf(" :: All ok, shutting down.\n");

	for (size_t i = 0; i < NUM_XDEVS; i++) {
		if (xdevs[i] == NULL) {
			continue;
		}

		printf("\tDestroying '%s'\n", xdevs[i]->str);
		xrt_device_destroy(&xdevs[i]);
	}

	// Finally done
	return do_exit(&xi, 0);
}
