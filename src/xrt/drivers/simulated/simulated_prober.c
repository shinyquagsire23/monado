// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simulated prober code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_simulated
 */

#include <stdio.h>
#include <stdlib.h>

#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "simulated_interface.h"

/*!
 * @implements xrt_auto_prober
 */
struct simulated_prober
{
	struct xrt_auto_prober base;
};

//! @private @memberof simulated_prober
static inline struct simulated_prober *
simulated_prober(struct xrt_auto_prober *p)
{
	return (struct simulated_prober *)p;
}

//! @public @memberof simulated_prober
static void
simulated_prober_destroy(struct xrt_auto_prober *p)
{
	struct simulated_prober *dp = simulated_prober(p);

	free(dp);
}

//! @public @memberof simulated_prober
static int
simulated_prober_autoprobe(struct xrt_auto_prober *xap,
                           cJSON *attached_data,
                           bool no_hmds,
                           struct xrt_prober *xp,
                           struct xrt_device **out_xdevs)
{
	struct simulated_prober *dp = simulated_prober(xap);
	(void)dp;

	// Do not create a simulated HMD if we are not looking for HMDs.
	if (no_hmds) {
		return 0;
	}

	out_xdevs[0] = simulated_hmd_create();
	return 1;
}

struct xrt_auto_prober *
simulated_create_auto_prober()
{
	struct simulated_prober *dp = U_TYPED_CALLOC(struct simulated_prober);
	dp->base.name = "Simulated";
	dp->base.destroy = simulated_prober_destroy;
	dp->base.lelo_dallas_autoprobe = simulated_prober_autoprobe;

	return &dp->base;
}
