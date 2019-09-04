// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Prints a list of found devices and tests opening some of them.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include <string.h>
#include <stdio.h>

#include "xrt/xrt_prober.h"
#include "cli_common.h"


static int
do_exit(struct xrt_prober **xp_ptr, int ret)
{
	xrt_prober_destroy(xp_ptr);

	printf(" :: Exiting '%i'\n", ret);

	return ret;
}

#define NUM_XDEVS 32

int
cli_cmd_test(int argc, const char **argv)
{
	struct xrt_device *xdevs[NUM_XDEVS] = {0};
	struct xrt_prober *xp = NULL;
	int ret = 0;

	// Initialize the prober.
	printf(" :: Creating prober!\n");

	ret = xrt_prober_create(&xp);
	if (ret != 0) {
		return do_exit(&xp, 0);
	}

	// Need to prime the prober with devices before dumping and listing.
	printf(" :: Probing!\n");

	ret = xrt_prober_probe(xp);
	if (ret != 0) {
		return do_exit(&xp, ret);
	}

	// So the user can see what we found.
	printf(" :: Dumping!\n");

	ret = xrt_prober_dump(xp);
	if (ret != 0) {
		do_exit(&xp, ret);
	}

	// Multiple devices can be found.
	printf(" :: Selecting devices!\n");

	ret = xrt_prober_select(xp, xdevs, NUM_XDEVS);
	if (ret != 0) {
		do_exit(&xp, ret);
	}
	if (xdevs[0] == NULL) {
		printf("\tNo HMD found! :(\n");
		return do_exit(&xp, -1);
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

		xdevs[i]->destroy(xdevs[i]);
		xdevs[i] = NULL;
	}

	// Finally done
	return do_exit(&xp, 0);
}
