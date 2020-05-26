// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared default implementation of the instance.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "target_lists.h"

#include "xrt/xrt_prober.h"
#include "xrt/xrt_instance.h"
#include "xrt/xrt_gfx_fd.h"

#include "util/u_misc.h"


/*
 *
 * Struct and helpers.
 *
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

static int
t_instance_select(struct xrt_instance *xinst,
                  struct xrt_device **xdevs,
                  size_t num_xdevs)
{
	struct t_instance *tinst = t_instance(xinst);

	int ret = xrt_prober_probe(tinst->xp);
	if (ret < 0) {
		return ret;
	}

	return xrt_prober_select(tinst->xp, xdevs, num_xdevs);
}

static int
t_instance_create_fd_compositor(struct xrt_instance *xinst,
                                struct xrt_device *xdev,
                                bool flip_y,
                                struct xrt_compositor_fd **out_xcfd)
{
	struct xrt_compositor_fd *xcfd =
	    xrt_gfx_provider_create_fd(xdev, flip_y);

	if (xcfd == NULL) {
		return -1;
	}

	*out_xcfd = xcfd;

	return 0;
}

static int
t_instance_get_prober(struct xrt_instance *xinst, struct xrt_prober **out_xp)
{
	struct t_instance *tinst = t_instance(xinst);

	*out_xp = tinst->xp;

	return 0;
}

static void
t_instance_destroy(struct xrt_instance *xinst)
{
	struct t_instance *tinst = t_instance(xinst);

	xrt_prober_destroy(&tinst->xp);
	free(tinst);
}


/*
 *
 * Exported function(s).
 *
 */

int
xrt_instance_create(struct xrt_instance **out_xinst)
{
	struct xrt_prober *xp = NULL;

	int ret = xrt_prober_create_with_lists(&xp, &target_lists);
	if (ret < 0) {
		return ret;
	}

	struct t_instance *tinst = U_TYPED_CALLOC(struct t_instance);
	tinst->base.select = t_instance_select;
	tinst->base.create_fd_compositor = t_instance_create_fd_compositor;
	tinst->base.get_prober = t_instance_get_prober;
	tinst->base.destroy = t_instance_destroy;
	tinst->xp = xp;

	*out_xinst = &tinst->base;

	return 0;
}
