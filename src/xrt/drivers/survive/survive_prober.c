// Copyright 2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  libsurvive prober code.
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @ingroup drv_survive
 */


#include "xrt/xrt_prober.h"

#include "util/u_misc.h"

#include "survive_interface.h"
#include "survive_driver.h"

/*!
 * @implements xrt_auto_prober
 */
struct survive_prober
{
	struct xrt_auto_prober base;
};

//! @private @memberof survive_prober
static inline struct survive_prober *
survive_prober(struct xrt_auto_prober *p)
{
	return (struct survive_prober *)p;
}

//! @public @memberof survive_prober
static void
survive_prober_destroy(struct xrt_auto_prober *p)
{
	struct survive_prober *survive_p = survive_prober(p);

	free(survive_p);
}

struct xrt_auto_prober *
survive_create_auto_prober()
{
	struct survive_prober *survive_p = U_TYPED_CALLOC(struct survive_prober);
	survive_p->base.name = "survive";
	survive_p->base.destroy = survive_prober_destroy;
	survive_p->base.lelo_dallas_autoprobe = survive_device_autoprobe;

	return &survive_p->base;
}
