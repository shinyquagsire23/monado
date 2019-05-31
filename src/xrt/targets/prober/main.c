// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  A program to help test the probing code in Monado.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include <string.h>
#include <stdio.h>

#include "target_lists.h"


static int
ps3_eye_found(struct xrt_prober *xp,
              struct xrt_prober_device **devices,
              size_t index,
              struct xrt_device **out_xdev)
{
	printf("Found PS3 Eye!\n");
	return 0;
}

struct xrt_prober_entry quirks_list[] = {
    {0x1415, 0x2000, ps3_eye_found, "PS3 Eye"},
    {0x0000, 0x0000, NULL, NULL}, // Terminate
};

struct xrt_prober_entry *entry_lists[] = {
    quirks_list, target_entry_list,
    NULL, // Terminate
};

struct xrt_prober_entry_lists list = {
    entry_lists,
    target_auto_list,
    NULL,
};

int
xrt_prober_create(struct xrt_prober **out_xp)
{
	return xrt_prober_create_with_lists(out_xp, &list);
}

int
do_exit(struct xrt_prober **xp, int ret)
{
	if (*xp != NULL) {
		(*xp)->destroy(xp);
	}

	printf(" :: Exiting '%i'\n", ret);
	return ret;
}

#define NUM_XDEVS 32

int
main(int argc, const char **argv)
{
	struct xrt_device *xdevs[NUM_XDEVS] = {0};
	struct xrt_prober *p = NULL;
	int ret = 0;

	printf(" :: Creating prober!\n");

	ret = xrt_prober_create(&p);
	if (ret != 0) {
		return ret;
	}

	printf(" :: Probing!\n");

	ret = p->probe(p);
	if (ret != 0) {
		return do_exit(&p, ret);
	}

	printf(" :: Dumping!\n");

	ret = p->dump(p);
	if (ret != 0) {
		do_exit(&p, ret);
	}

	printf(" :: Selecting device!\n");

	ret = p->select(p, xdevs, NUM_XDEVS);
	if (ret != 0) {
		do_exit(&p, ret);
	}
	if (xdevs[0] == NULL) {
		printf("\tNo HMD found! :(\n");
		return do_exit(&p, -1);
	}


	for (size_t i = 0; i < NUM_XDEVS; i++) {
		if (xdevs[i] == NULL) {
			continue;
		}

		printf("\tFound '%s'\n", xdevs[i]->name);

		xdevs[i]->destroy(xdevs[i]);
		xdevs[i] = NULL;
	}

	printf(" :: All ok, shutting down.\n");

	return do_exit(&p, 0);
}
