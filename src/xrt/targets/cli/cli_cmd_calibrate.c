// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Prints a list of found devices and tests opening some of them.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "xrt/xrt_prober.h"
#include "util/u_misc.h"
#include "cli_common.h"


struct program
{
	struct xrt_prober *xp;

	int index;
	int selected;
};

static int
init(struct program *p)
{
	int ret;

	// Fist initialize the prober.
	ret = xrt_prober_create(&p->xp);
	if (ret != 0) {
		fprintf(stderr, "Failed to create prober\n");
		return ret;
	}

	// Need to prime the prober before listing devices.
	ret = xrt_prober_probe(p->xp);
	if (ret != 0) {
		fprintf(stderr, "Failed to probe for devices.\n");
		return ret;
	}

	return 0;
}

static void
list_cb(struct xrt_prober *xp,
        struct xrt_prober_device *pdev,
        const char *name,
        void *ptr)
{
	struct program *p = (struct program *)ptr;
	if (p->selected <= 0) {
		printf(" %i) %s\n", ++p->index, name);
	} else if (p->selected == ++p->index) {
		// Do stuff
		printf(" :: Doing calibrartion\n");
		printf(" Pretending to calibrarating camera '%s'\n", name);
	}
}

static int
print_cameras(struct program *p)
{
	char *buffer = NULL;
	size_t buffer_size = 0;
	int selected = -1;
	ssize_t len;
	int ret;

	p->index = 0;
	ret = xrt_prober_list_video_devices(p->xp, list_cb, p);
	if (ret != 0) {
		return ret;
	}

	if (p->index <= 0) {
		printf("\tNo video devices found!\n");
		return -1;
	}

	printf("Please select camera: ");
	fflush(stdout);

	len = getline(&buffer, &buffer_size, stdin);

	if (buffer && len >= 1) {
		selected = (int)strtol(buffer, NULL, 10);
	}

	if (selected < 1 || selected > p->index) {
		printf("Invalid camera! %*.s", (int)len, buffer);
		free(buffer);
		return -1;
	}
	free(buffer);

	p->index = 0;
	p->selected = selected;
	ret = xrt_prober_list_video_devices(p->xp, list_cb, p);
	if (ret != 0) {
		return ret;
	}


	return 0;
}

static int
do_exit(struct program *p, int ret)
{
	if (p->xp != NULL) {
		xrt_prober_destroy(&p->xp);
	}

	printf(" :: Exiting '%i'\n", ret);

	return ret;
}

int
cli_cmd_calibrate(int argc, const char **argv)
{
	struct program p = {0};
	int ret;

	printf(" :: Starting!\n");

	// Init the prober and other things.
	ret = init(&p);
	if (ret != 0) {
		return do_exit(&p, ret);
	}

	// List the cameras found.
	ret = print_cameras(&p);
	if (ret != 0) {
		return do_exit(&p, ret);
	}

	return do_exit(&p, 0);
}
