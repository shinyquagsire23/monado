// Copyright 2019, Collabora, Ltd.
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


DEBUG_GET_ONCE_BOOL_OPTION(ns_spew, "NS_PRINT_SPEW", false)
DEBUG_GET_ONCE_BOOL_OPTION(ns_debug, "NS_PRINT_DEBUG", false)

struct ns_prober
{
	struct xrt_auto_prober base;
	bool print_spew;
	bool print_debug;
};

static inline struct ns_prober *
ns_prober(struct xrt_auto_prober *p)
{
	return (struct ns_prober *)p;
}

static void
ns_prober_destroy(struct xrt_auto_prober *p)
{
	struct ns_prober *nsp = ns_prober(p);

	free(nsp);
}

static struct xrt_device *
ns_prober_autoprobe(struct xrt_auto_prober *xap,
                    bool no_hmds,
                    struct xrt_prober *xp)
{
	struct ns_prober *nsp = ns_prober(xap);

	if (no_hmds) {
		return NULL;
	}

	return ns_hmd_create(nsp->print_spew, nsp->print_debug);
}

struct xrt_auto_prober *
ns_create_auto_prober()
{
	struct ns_prober *nsp = U_TYPED_CALLOC(struct ns_prober);
	nsp->base.destroy = ns_prober_destroy;
	nsp->base.lelo_dallas_autoprobe = ns_prober_autoprobe;
	nsp->print_spew = debug_get_bool_option_ns_spew();
	nsp->print_debug = debug_get_bool_option_ns_debug();

	return &nsp->base;
}
