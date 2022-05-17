// Copyright 2019-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Enable the use of the prober in the gui application.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "xrt/xrt_prober.h"
#include "xrt/xrt_instance.h"
#include "xrt/xrt_system.h"
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
	xrt_result_t xret;

	// Initialize the prober.
	xret = xrt_instance_create(NULL, &p->instance);
	if (xret != 0) {
		return do_exit(p, -1);
	}

	// Still need the prober to get video devices.
	xret = xrt_instance_get_prober(p->instance, &p->xp);
	if (xret != XRT_SUCCESS) {
		return do_exit(p, -1);
	}

	if (p->xp != NULL) {
		// Need to prime the prober with devices before dumping and listing.
		xret = xrt_prober_probe(p->xp);
		if (xret != XRT_SUCCESS) {
			return do_exit(p, -1);
		}
	}

	return 0;
}

int
gui_prober_select(struct gui_program *p)
{
	xrt_result_t xret = xrt_instance_create_system(p->instance, &p->xsysd, NULL);
	if (xret != XRT_SUCCESS) {
		return -1;
	}

	return 0;
}

void
gui_prober_update(struct gui_program *p)
{
	if (!p->xsysd) {
		return;
	}

	for (size_t i = 0; i < p->xsysd->xdev_count; i++) {
		if (p->xsysd->xdevs[i] == NULL) {
			continue;
		}
		xrt_device_update_inputs(p->xsysd->xdevs[i]);
	}
}

void
gui_prober_teardown(struct gui_program *p)
{
	xrt_system_devices_destroy(&p->xsysd);

	xrt_instance_destroy(&p->instance);
}
