// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Simulated prober code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_wivrn
 */

#include <stdio.h>
#include <stdlib.h>

#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "wivrn_interface.h"
#include "wivrn_hmd.h"
#include "wivrn_controller.h"
#include "wivrn_session.h"
#include <memory>

/*!
 * @implements xrt_auto_prober
 */
struct wivrn_prober
{
	struct xrt_auto_prober base;
};

//! @private @memberof wivrn_prober
static inline struct wivrn_prober *
wivrn_prober(struct xrt_auto_prober *p)
{
	return (struct wivrn_prober *)p;
}

//! @public @memberof wivrn_prober
static void
wivrn_prober_destroy(struct xrt_auto_prober *p)
{
	struct wivrn_prober *dp = wivrn_prober(p);

	free(dp);
}

//! @public @memberof wivrn_prober
static int
wivrn_prober_autoprobe(struct xrt_auto_prober *xap,
                       cJSON *attached_data,
                       bool no_hmds,
                       struct xrt_prober *xp,
                       struct xrt_device **out_xdevs)
{
	struct wivrn_prober *dp = wivrn_prober(xap);
	(void)dp;

	// Do not create a wivrn HMD if we are not looking for HMDs.
	if (no_hmds) {
		return 0;
	}

	printf("Waiting for handshake from HMD\n");

	try {
		return xrt::drivers::wivrn::wivrn_session::wait_for_handshake(out_xdevs);
	} catch (std::exception &e) {
		U_LOG_E("Error waiting for handshake: %s", e.what());
		return 0;
	}
}

struct xrt_auto_prober *
wivrn_create_auto_prober()
{
	struct wivrn_prober *dp = U_TYPED_CALLOC(struct wivrn_prober);
	dp->base.name = "WiVRn";
	dp->base.destroy = wivrn_prober_destroy;
	dp->base.lelo_dallas_autoprobe = wivrn_prober_autoprobe;

	return &dp->base;
}
