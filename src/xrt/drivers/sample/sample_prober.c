// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Sample prober code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_sample
 */

#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "sample_interface.h"


/*!
 * @implements xrt_auto_prober
 */
struct sample_auto_prober
{
	struct xrt_auto_prober base;
};

//! @private @memberof sample_auto_prober
static inline struct sample_auto_prober *
sample_auto_prober(struct xrt_auto_prober *p)
{
	return (struct sample_auto_prober *)p;
}

//! @private @memberof sample_auto_prober
static void
sample_auto_prober_destroy(struct xrt_auto_prober *p)
{
	struct sample_auto_prober *sap = sample_auto_prober(p);

	free(sap);
}

//! @public @memberof sample_auto_prober
static int
sample_auto_prober_autoprobe(struct xrt_auto_prober *xap,
                             cJSON *attached_data,
                             bool no_hmds,
                             struct xrt_prober *xp,
                             struct xrt_device **out_xdevs)
{
	struct sample_auto_prober *sap = sample_auto_prober(xap);
	(void)sap;

	// Do not create a sample HMD if we are not looking for HMDs.
	if (no_hmds) {
		return 0;
	}

	out_xdevs[0] = sample_hmd_create();
	return 1;
}

struct xrt_auto_prober *
sample_create_auto_prober(void)
{
	struct sample_auto_prober *sap = U_TYPED_CALLOC(struct sample_auto_prober);
	sap->base.name = "Sample";
	sap->base.destroy = sample_auto_prober_destroy;
	sap->base.lelo_dallas_autoprobe = sample_auto_prober_autoprobe;

	return &sap->base;
}
