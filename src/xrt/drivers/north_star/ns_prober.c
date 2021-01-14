// Copyright 2019-2020, Collabora, Ltd.
// Copyright 2020, Nova King.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  North Star prober code.
 * @author Nova King <technobaboo@gmail.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_ns
 */

#include <stdio.h>
#include <stdlib.h>

#include "xrt/xrt_prober.h"

#include "util/u_misc.h"
#include "util/u_debug.h"

#include "ns_interface.h"


DEBUG_GET_ONCE_OPTION(ns_config_path, "NS_CONFIG_PATH", NULL)

/*!
 * @implements xrt_auto_prober
 */
struct ns_prober
{
	struct xrt_auto_prober base;
	const char *config_path;
};

//! @private @memberof ns_prober
static inline struct ns_prober *
ns_prober(struct xrt_auto_prober *p)
{
	return (struct ns_prober *)p;
}

//! @public @memberof ns_prober
static void
ns_prober_destroy(struct xrt_auto_prober *p)
{
	struct ns_prober *nsp = ns_prober(p);

	free(nsp);
}

//! @public @memberof ns_prober
static struct xrt_device *
ns_prober_autoprobe(struct xrt_auto_prober *xap, cJSON *attached_data, bool no_hmds, struct xrt_prober *xp)
{
	struct ns_prober *nsp = ns_prober(xap);

	if (no_hmds) {
		return NULL;
	}

	if (nsp->config_path == NULL) {
		return NULL;
	}

	return ns_hmd_create(nsp->config_path);
}

struct xrt_auto_prober *
ns_create_auto_prober()
{
	struct ns_prober *nsp = U_TYPED_CALLOC(struct ns_prober);
	nsp->base.destroy = ns_prober_destroy;
	nsp->base.lelo_dallas_autoprobe = ns_prober_autoprobe;
	nsp->config_path = debug_get_option_ns_config_path();

	return &nsp->base;
}
