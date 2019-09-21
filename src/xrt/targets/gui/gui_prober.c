// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Enable the use of the prober in the gui application.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "util/u_time.h"
#include "target_lists.h"
#include "gui_common.h"


/*
 *
 * Helper functions.
 *
 */

static int
do_exit(struct program *p, int ret)
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
gui_prober_init(struct program *p)
{
	int ret = 0;

	p->timekeeping = time_state_create();

	// Initialize the prober.
	ret = xrt_prober_create(&p->xp);
	if (ret != 0) {
		return do_exit(p, ret);
	}

	// Need to prime the prober with devices before dumping and listing.
	ret = xrt_prober_probe(p->xp);
	if (ret != 0) {
		return do_exit(p, ret);
	}

	return 0;
}

int
gui_prober_select(struct program *p)
{
	int ret;

	// Multiple devices can be found.
	ret = xrt_prober_select(p->xp, p->xdevs, NUM_XDEVS);
	if (ret != 0) {
		return ret;
	}

	return 0;
}

void
gui_prober_update(struct program *p)
{
	// We haven't been initialized
	if (p->timekeeping == NULL) {
		return;
	}

	time_state_get_now_and_update(p->timekeeping);

	for (size_t i = 0; i < NUM_XDEVS; i++) {
		if (p->xdevs[i] == NULL) {
			continue;
		}

		p->xdevs[i]->update_inputs(p->xdevs[i], p->timekeeping);
	}
}

void
gui_prober_teardown(struct program *p)
{
	for (size_t i = 0; i < NUM_XDEVS; i++) {
		if (p->xdevs[i] == NULL) {
			continue;
		}

		p->xdevs[i]->destroy(p->xdevs[i]);
		p->xdevs[i] = NULL;
	}

	if (p->timekeeping != NULL) {
		time_state_destroy(p->timekeeping);
		p->timekeeping = NULL;
	}

	xrt_prober_destroy(&p->xp);
}

int
xrt_prober_create(struct xrt_prober **out_xp)
{
	return xrt_prober_create_with_lists(out_xp, &target_lists);
}
