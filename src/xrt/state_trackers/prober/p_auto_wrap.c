// Copyright 2019, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Export a auto prober interface.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup st_prober
 */

#include "xrt/xrt_compiler.h"
#include "xrt/xrt_prober.h"
#include "util/u_misc.h"


/*
 *
 * Structs and helprs.
 *
 */

/*!
 * Simple wrapper exposing the old interface.
 *
 * @ingroup st_prober
 */
struct prober_wrapper
{
	struct xrt_auto_prober base;
	struct xrt_prober *xp;
};

static inline struct prober_wrapper *
prober_wrapper(struct xrt_auto_prober *xap)
{
	return (struct prober_wrapper *)xap;
}


/*
 *
 * Member functions.
 *
 */

static struct xrt_device *
auto_probe(struct xrt_auto_prober *xap)
{
	struct prober_wrapper *pw = prober_wrapper(xap);
	struct xrt_device *xdev = NULL;
	int ret;

	ret = pw->xp->probe(pw->xp);
	if (ret < 0) {
		return NULL;
	}

	ret = pw->xp->select(pw->xp, &xdev, 1);
	if (ret < 0) {
		return NULL;
	}

	return xdev;
}

static void
destroy(struct xrt_auto_prober *xap)
{
	struct prober_wrapper *pw = prober_wrapper(xap);

	if (pw->xp != NULL) {
		pw->xp->destroy(&pw->xp);
	}

	free(pw);
}


/*
 *
 * Exported function(s).
 *
 */

struct xrt_auto_prober *
xrt_auto_prober_create()
{
	struct prober_wrapper *pw = U_TYPED_CALLOC(struct prober_wrapper);
	struct xrt_prober *xp = NULL;
	int ret;

	if (pw == NULL) {
		return NULL;
	}

	ret = xrt_prober_create(&xp);
	if (ret < 0) {
		free(pw);
		return NULL;
	}

	pw->base.lelo_dallas_autoprobe = auto_probe;
	pw->base.destroy = destroy;
	pw->xp = xp;

	return &pw->base;
}
