// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared default implementation of the instance: pieces that are used
 * whether or not there's a compositor.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */
#pragma once

#include "target_lists.h"

#include "xrt/xrt_prober.h"
#include "xrt/xrt_instance.h"

#include "util/u_misc.h"
#include "util/u_trace_marker.h"


/*
 *
 * Struct and helpers.
 *
 */

/*!
 * Main "real" instance implementation.
 *
 * Used in instances both with and without compositor usage.
 *
 * @implements xrt_instance
 */
struct t_instance
{
	struct xrt_instance base;
	struct xrt_prober *xp;
};

static inline struct t_instance *
t_instance(struct xrt_instance *xinst)
{
	return (struct t_instance *)xinst;
}


/*
 *
 * Member functions.
 *
 */

static xrt_result_t
t_instance_get_prober(struct xrt_instance *xinst, struct xrt_prober **out_xp)
{
	XRT_TRACE_MARKER();

	struct t_instance *tinst = t_instance(xinst);

	if (tinst->xp == NULL) {
		return XRT_ERROR_PROBER_NOT_SUPPORTED;
	}

	*out_xp = tinst->xp;

	return XRT_SUCCESS;
}

static void
t_instance_destroy(struct xrt_instance *xinst)
{
	XRT_TRACE_MARKER();

	struct t_instance *tinst = t_instance(xinst);

	xrt_prober_destroy(&tinst->xp);
	free(tinst);
}
