// Copyright 2019-2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  OpenHMD prober code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ohmd
 */

#include <stdio.h>
#include <stdlib.h>

#include "openhmd.h"
#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "oh_interface.h"
#include "oh_device.h"


/*!
 * @implements xrt_auto_prober
 */
struct oh_prober
{
	struct xrt_auto_prober base;
	ohmd_context *ctx;
};

//! @private @memberof oh_prober
static inline struct oh_prober *
oh_prober(struct xrt_auto_prober *p)
{
	return (struct oh_prober *)p;
}

//! @public @memberof oh_prober
static void
oh_prober_destroy(struct xrt_auto_prober *p)
{
	struct oh_prober *ohp = oh_prober(p);

	if (ohp->ctx != NULL) {
		ohmd_ctx_destroy(ohp->ctx);
		ohp->ctx = NULL;
	}

	free(ohp);
}

//! @public @memberof oh_prober
static int
oh_prober_autoprobe(struct xrt_auto_prober *xap,
                    cJSON *attached_data,
                    bool no_hmds,
                    struct xrt_prober *xp,
                    struct xrt_device **out_xdevs)
{
	struct oh_prober *ohp = oh_prober(xap);

	int num_created = oh_device_create(ohp->ctx, no_hmds, out_xdevs);
	return num_created;
}

struct xrt_auto_prober *
oh_create_auto_prober()
{
	struct oh_prober *ohp = U_TYPED_CALLOC(struct oh_prober);
	ohp->base.name = "OpenHMD";
	ohp->base.destroy = oh_prober_destroy;
	ohp->base.lelo_dallas_autoprobe = oh_prober_autoprobe;
	ohp->ctx = ohmd_ctx_create();

	return &ohp->base;
}
