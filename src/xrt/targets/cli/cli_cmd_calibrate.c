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
#include <sys/types.h>

#include "xrt/xrt_instance.h"
#include "xrt/xrt_prober.h"
#include "util/u_misc.h"
#include "cli_common.h"


struct program
{
	struct xrt_instance *xi;
	struct xrt_prober *xp;

	int index;
	int selected;
};

static int
init(struct program *p)
{
	int ret;

	// Fist initialize the instance.
	ret = xrt_instance_create(NULL, &p->xi);
	if (ret != 0) {
		fprintf(stderr, "Failed to create instance\n");
		return ret;
	}

	// Get the prober pointer.
	// In general, null probers are OK, but this module directly uses the
	// prober.
	ret = xrt_instance_get_prober(p->xi, &p->xp);
	if (ret != 0) {
		fprintf(stderr, "Failed to get prober from instance.\n");
		return ret;
	}
	if (p->xp == NULL) {
		fprintf(stderr, "Null prober returned - cannot proceed.\n");
		return -1;
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
        const char *product,
        const char *manufacturer,
        const char *serial,
        void *ptr)
{
	struct program *p = (struct program *)ptr;
	if (p->selected <= 0) {
		printf(" %i) %s\n", ++p->index, product);
	} else if (p->selected == ++p->index) {
		// Do stuff
		printf(" :: Doing calibration\n");
		printf(" Pretending to calibrate camera '%s'\n", product);
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
	p->xp = NULL;
	xrt_instance_destroy(&p->xi);

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
