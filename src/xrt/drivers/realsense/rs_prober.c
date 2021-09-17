// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Realsense prober code.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_rs
 */

#include <stdio.h>
#include <stdlib.h>

#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "rs_interface.h"

/*!
 * @implements xrt_auto_prober
 */
struct rs_prober
{
	struct xrt_auto_prober base;
};

//! @private @memberof rs_prober
static inline struct rs_prober *
rs_prober(struct xrt_auto_prober *p)
{
	return (struct rs_prober *)p;
}

//! @public @memberof rs_prober
static void
rs_prober_destroy(struct xrt_auto_prober *p)
{
	struct rs_prober *dp = rs_prober(p);

	free(dp);
}

//! @public @memberof rs_prober
static int
rs_prober_autoprobe(struct xrt_auto_prober *xap,
                    cJSON *attached_data,
                    bool no_hmds,
                    struct xrt_prober *xp,
                    struct xrt_device **out_xdevs)
{
	struct rs_prober *dp = rs_prober(xap);
	(void)dp;

	struct xrt_device *dev = rs_ddev_create();
	if (!dev) {
		return 0;
	}

	out_xdevs[0] = dev;
	return 1;
}

struct xrt_auto_prober *
rs_create_auto_prober()
{
	struct rs_prober *dp = U_TYPED_CALLOC(struct rs_prober);
	dp->base.name = "Realsense";
	dp->base.destroy = rs_prober_destroy;
	dp->base.lelo_dallas_autoprobe = rs_prober_autoprobe;

	return &dp->base;
}
