// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Enable the use of the prober in the gui application.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_prober.h"
#include "xrt/xrt_instance.h"
#include "util/u_time.h"
#include "gui_common.h"


/*
 *
 * Helper functions.
 *
 */

static int
do_exit(struct gui_program *p, int ret)
{
	gui_prober_teardown(p);
	return ret;
}


/*
 *
 * 'Exported' functions.
 *
 */

int
gui_prober_init(struct gui_program *p)
{
	int ret = 0;

	// Initialize the prober.
	ret = xrt_instance_create(NULL, &p->instance);
	if (ret != 0) {
		return do_exit(p, ret);
	}
	ret = xrt_instance_get_prober(p->instance, &p->xp);
	if (ret != 0) {
		return do_exit(p, ret);
	}

	if (p->xp != NULL) {
		// Need to prime the prober with devices before dumping and
		// listing.
		ret = xrt_prober_probe(p->xp);
		if (ret != 0) {
			return do_exit(p, ret);
		}
	}
	return 0;
}

int
gui_prober_select(struct gui_program *p)
{
	int ret;

	// Multiple devices can be found.
	ret = xrt_instance_select(p->instance, p->xdevs, NUM_XDEVS);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

void
gui_prober_update(struct gui_program *p)
{
	for (size_t i = 0; i < NUM_XDEVS; i++) {
		if (p->xdevs[i] == NULL) {
			continue;
		}
		xrt_device_update_inputs(p->xdevs[i]);
	}
}

void
gui_prober_teardown(struct gui_program *p)
{
	for (size_t i = 0; i < NUM_XDEVS; i++) {
		if (p->xdevs[i] == NULL) {
			continue;
		}

		xrt_device_destroy(&(p->xdevs[i]));
	}

	xrt_instance_destroy(&p->instance);
}
