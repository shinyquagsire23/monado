// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Shared default implementation of the instance, but with no compositor
 * usage
 * @author Jakob Bornecrantz <jakob@collabora.com>
 */

#include "target_instance_parts.h"


static int
t_instance_create_fd_compositor_stub(struct xrt_instance *xinst,
                                     struct xrt_device *xdev,
                                     bool flip_y,
                                     struct xrt_compositor_fd **out_xcfd)
{
	*out_xcfd = NULL;

	return -1;
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
	tinst->base.create_fd_compositor = t_instance_create_fd_compositor_stub;
	tinst->base.get_prober = t_instance_get_prober;
	tinst->base.destroy = t_instance_destroy;
	tinst->xp = xp;

	*out_xinst = &tinst->base;

	return 0;
}
